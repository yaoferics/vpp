/*
 *------------------------------------------------------------------
 * svm_queue.c - unidirectional shared-memory queues
 *
 * Copyright (c) 2009 Cisco and/or its affiliates.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *------------------------------------------------------------------
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <vppinfra/mem.h>
#include <vppinfra/format.h>
#include <vppinfra/cache.h>
#include <svm/queue.h>
#include <vppinfra/time.h>
#include <signal.h>

/*
 * svm_queue_init
 *
 * nels = number of elements on the queue
 * elsize = element size, presumably 4 and cacheline-size will
 *          be popular choices.
 * pid   = consumer pid
 *
 * The idea is to call this function in the queue consumer,
 * and e-mail the queue pointer to the producer(s).
 *
 * The vpp process / main thread allocates one of these
 * at startup; its main input queue. The vpp main input queue
 * has a pointer to it in the shared memory segment header.
 *
 * You probably want to be on an svm data heap before calling this
 * function.
 */
svm_queue_t *
svm_queue_init (int nels,
		int elsize, int consumer_pid, int signal_when_queue_non_empty)
{
  svm_queue_t *q;
  pthread_mutexattr_t attr;
  pthread_condattr_t cattr;

  q = clib_mem_alloc_aligned (sizeof (svm_queue_t)
			      + nels * elsize, CLIB_CACHE_LINE_BYTES);
  memset (q, 0, sizeof (*q));

  q->elsize = elsize;
  q->maxsize = nels;
  q->consumer_pid = consumer_pid;
  q->signal_when_queue_non_empty = signal_when_queue_non_empty;

  memset (&attr, 0, sizeof (attr));
  memset (&cattr, 0, sizeof (cattr));

  if (pthread_mutexattr_init (&attr))
    clib_unix_warning ("mutexattr_init");
  if (pthread_mutexattr_setpshared (&attr, PTHREAD_PROCESS_SHARED))
    clib_unix_warning ("pthread_mutexattr_setpshared");
  if (pthread_mutex_init (&q->mutex, &attr))
    clib_unix_warning ("mutex_init");
  if (pthread_mutexattr_destroy (&attr))
    clib_unix_warning ("mutexattr_destroy");
  if (pthread_condattr_init (&cattr))
    clib_unix_warning ("condattr_init");
  /* prints funny-looking messages in the Linux target */
  if (pthread_condattr_setpshared (&cattr, PTHREAD_PROCESS_SHARED))
    clib_unix_warning ("condattr_setpshared");
  if (pthread_cond_init (&q->condvar, &cattr))
    clib_unix_warning ("cond_init1");
  if (pthread_condattr_destroy (&cattr))
    clib_unix_warning ("cond_init2");

  return (q);
}

/*
 * svm_queue_free
 */
void
svm_queue_free (svm_queue_t * q)
{
  (void) pthread_mutex_destroy (&q->mutex);
  (void) pthread_cond_destroy (&q->condvar);
  clib_mem_free (q);
}

void
svm_queue_lock (svm_queue_t * q)
{
  pthread_mutex_lock (&q->mutex);
}

void
svm_queue_unlock (svm_queue_t * q)
{
  pthread_mutex_unlock (&q->mutex);
}

int
svm_queue_is_full (svm_queue_t * q)
{
  return q->cursize == q->maxsize;
}

/*
 * svm_queue_add_nolock
 */
int
svm_queue_add_nolock (svm_queue_t * q, u8 * elem)
{
  i8 *tailp;
  int need_broadcast = 0;

  if (PREDICT_FALSE (q->cursize == q->maxsize))
    {
      while (q->cursize == q->maxsize)
	{
	  (void) pthread_cond_wait (&q->condvar, &q->mutex);
	}
    }

  tailp = (i8 *) (&q->data[0] + q->elsize * q->tail);
  clib_memcpy (tailp, elem, q->elsize);

  q->tail++;
  q->cursize++;

  need_broadcast = (q->cursize == 1);

  if (q->tail == q->maxsize)
    q->tail = 0;

  if (need_broadcast)
    {
      (void) pthread_cond_broadcast (&q->condvar);
      if (q->signal_when_queue_non_empty)
	kill (q->consumer_pid, q->signal_when_queue_non_empty);
    }
  return 0;
}

int
svm_queue_add_raw (svm_queue_t * q, u8 * elem)
{
  i8 *tailp;

  if (PREDICT_FALSE (q->cursize == q->maxsize))
    {
      while (q->cursize == q->maxsize)
	;
    }

  tailp = (i8 *) (&q->data[0] + q->elsize * q->tail);
  clib_memcpy (tailp, elem, q->elsize);

  q->tail++;
  q->cursize++;

  if (q->tail == q->maxsize)
    q->tail = 0;
  return 0;
}


/*
 * svm_queue_add
 */
int
svm_queue_add (svm_queue_t * q, u8 * elem, int nowait)
{
  i8 *tailp;
  int need_broadcast = 0;

  if (nowait)
    {
      /* zero on success */
      if (pthread_mutex_trylock (&q->mutex))
	{
	  return (-1);
	}
    }
  else
    pthread_mutex_lock (&q->mutex);

  if (PREDICT_FALSE (q->cursize == q->maxsize))
    {
      if (nowait)
	{
	  pthread_mutex_unlock (&q->mutex);
	  return (-2);
	}
      while (q->cursize == q->maxsize)
	{
	  (void) pthread_cond_wait (&q->condvar, &q->mutex);
	}
    }

  tailp = (i8 *) (&q->data[0] + q->elsize * q->tail);
  clib_memcpy (tailp, elem, q->elsize);

  q->tail++;
  q->cursize++;

  need_broadcast = (q->cursize == 1);

  if (q->tail == q->maxsize)
    q->tail = 0;

  if (need_broadcast)
    {
      (void) pthread_cond_broadcast (&q->condvar);
      if (q->signal_when_queue_non_empty)
	kill (q->consumer_pid, q->signal_when_queue_non_empty);
    }
  pthread_mutex_unlock (&q->mutex);

  return 0;
}

/*
 * svm_queue_add2
 */
int
svm_queue_add2 (svm_queue_t * q, u8 * elem, u8 * elem2, int nowait)
{
  i8 *tailp;
  int need_broadcast = 0;

  if (nowait)
    {
      /* zero on success */
      if (pthread_mutex_trylock (&q->mutex))
	{
	  return (-1);
	}
    }
  else
    pthread_mutex_lock (&q->mutex);

  if (PREDICT_FALSE (q->cursize + 1 == q->maxsize))
    {
      if (nowait)
	{
	  pthread_mutex_unlock (&q->mutex);
	  return (-2);
	}
      while (q->cursize + 1 == q->maxsize)
	{
	  (void) pthread_cond_wait (&q->condvar, &q->mutex);
	}
    }

  tailp = (i8 *) (&q->data[0] + q->elsize * q->tail);
  clib_memcpy (tailp, elem, q->elsize);

  q->tail++;
  q->cursize++;

  if (q->tail == q->maxsize)
    q->tail = 0;

  need_broadcast = (q->cursize == 1);

  tailp = (i8 *) (&q->data[0] + q->elsize * q->tail);
  clib_memcpy (tailp, elem2, q->elsize);

  q->tail++;
  q->cursize++;

  if (q->tail == q->maxsize)
    q->tail = 0;

  if (need_broadcast)
    {
      (void) pthread_cond_broadcast (&q->condvar);
      if (q->signal_when_queue_non_empty)
	kill (q->consumer_pid, q->signal_when_queue_non_empty);
    }
  pthread_mutex_unlock (&q->mutex);

  return 0;
}

/*
 * svm_queue_sub
 */
int
svm_queue_sub (svm_queue_t * q, u8 * elem, svm_q_conditional_wait_t cond,
	       u32 time)
{
  i8 *headp;
  int need_broadcast = 0;
  int rc = 0;

  if (cond == SVM_Q_NOWAIT)
    {
      /* zero on success */
      if (pthread_mutex_trylock (&q->mutex))
	{
	  return (-1);
	}
    }
  else
    pthread_mutex_lock (&q->mutex);

  if (PREDICT_FALSE (q->cursize == 0))
    {
      if (cond == SVM_Q_NOWAIT)
	{
	  pthread_mutex_unlock (&q->mutex);
	  return (-2);
	}
      else if (cond == SVM_Q_TIMEDWAIT)
	{
	  struct timespec ts;
	  ts.tv_sec = unix_time_now () + time;
	  ts.tv_nsec = 0;
	  while (q->cursize == 0 && rc == 0)
	    {
	      rc = pthread_cond_timedwait (&q->condvar, &q->mutex, &ts);
	    }
	  if (rc == ETIMEDOUT)
	    {
	      pthread_mutex_unlock (&q->mutex);
	      return ETIMEDOUT;
	    }
	}
      else
	{
	  while (q->cursize == 0)
	    {
	      (void) pthread_cond_wait (&q->condvar, &q->mutex);
	    }
	}
    }

  headp = (i8 *) (&q->data[0] + q->elsize * q->head);
  clib_memcpy (elem, headp, q->elsize);

  q->head++;
  /* $$$$ JFC shouldn't this be == 0? */
  if (q->cursize == q->maxsize)
    need_broadcast = 1;

  q->cursize--;

  if (q->head == q->maxsize)
    q->head = 0;

  if (need_broadcast)
    (void) pthread_cond_broadcast (&q->condvar);

  pthread_mutex_unlock (&q->mutex);

  return 0;
}

int
svm_queue_sub2 (svm_queue_t * q, u8 * elem)
{
  int need_broadcast;
  i8 *headp;

  pthread_mutex_lock (&q->mutex);
  if (q->cursize == 0)
    {
      pthread_mutex_unlock (&q->mutex);
      return -1;
    }

  headp = (i8 *) (&q->data[0] + q->elsize * q->head);
  clib_memcpy (elem, headp, q->elsize);

  q->head++;
  need_broadcast = (q->cursize == q->maxsize / 2);
  q->cursize--;

  if (PREDICT_FALSE (q->head == q->maxsize))
    q->head = 0;
  pthread_mutex_unlock (&q->mutex);

  if (need_broadcast)
    (void) pthread_cond_broadcast (&q->condvar);

  return 0;
}

int
svm_queue_sub_raw (svm_queue_t * q, u8 * elem)
{
  i8 *headp;

  if (PREDICT_FALSE (q->cursize == 0))
    {
      while (q->cursize == 0)
	;
    }

  headp = (i8 *) (&q->data[0] + q->elsize * q->head);
  clib_memcpy (elem, headp, q->elsize);

  q->head++;
  q->cursize--;

  if (q->head == q->maxsize)
    q->head = 0;
  return 0;
}

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
