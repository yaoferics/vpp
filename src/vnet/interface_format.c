/*
 * Copyright (c) 2015 Cisco and/or its affiliates.
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
 */
/*
 * interface_format.c: interface formatting
 *
 * Copyright (c) 2008 Eliot Dresselhaus
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 *  LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 *  OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 *  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <vnet/vnet.h>
#include <vppinfra/bitmap.h>

u8 *
format_vnet_sw_interface_flags (u8 * s, va_list * args)
{
  u32 flags = va_arg (*args, u32);

  if (flags & VNET_SW_INTERFACE_FLAG_ERROR)
    s = format (s, "error");
  else if (flags & VNET_SW_INTERFACE_FLAG_BOND_SLAVE)
    s = format (s, "bond-slave");
  else
    {
      s = format (s, "%s",
		  (flags & VNET_SW_INTERFACE_FLAG_ADMIN_UP) ? "up" : "down");
      if (flags & VNET_SW_INTERFACE_FLAG_PUNT)
	s = format (s, "/punt");
    }

  return s;
}

u8 *
format_vnet_hw_interface_rx_mode (u8 * s, va_list * args)
{
  vnet_hw_interface_rx_mode mode = va_arg (*args, vnet_hw_interface_rx_mode);

  if (mode == VNET_HW_INTERFACE_RX_MODE_POLLING)
    return format (s, "polling");

  if (mode == VNET_HW_INTERFACE_RX_MODE_INTERRUPT)
    return format (s, "interrupt");

  if (mode == VNET_HW_INTERFACE_RX_MODE_ADAPTIVE)
    return format (s, "adaptive");

  return format (s, "unknown");
}

u8 *
format_vnet_hw_interface (u8 * s, va_list * args)
{
  vnet_main_t *vnm = va_arg (*args, vnet_main_t *);
  vnet_hw_interface_t *hi = va_arg (*args, vnet_hw_interface_t *);
  vnet_hw_interface_class_t *hw_class;
  vnet_device_class_t *dev_class;
  int verbose = va_arg (*args, int);
  u32 indent;

  if (!hi)
    return format (s, "%=32s%=6s%=8s%s", "Name", "Idx", "Link", "Hardware");

  indent = format_get_indent (s);

  s = format (s, "%-32v%=6d", hi->name, hi->hw_if_index);

  if (hi->bond_info == VNET_HW_INTERFACE_BOND_INFO_SLAVE)
    s = format (s, "%=8s", "slave");
  else
    s = format (s, "%=8s",
		hi->flags & VNET_HW_INTERFACE_FLAG_LINK_UP ? "up" : "down");

  hw_class = vnet_get_hw_interface_class (vnm, hi->hw_class_index);
  dev_class = vnet_get_device_class (vnm, hi->dev_class_index);

  if (hi->bond_info && (hi->bond_info != VNET_HW_INTERFACE_BOND_INFO_SLAVE))
    {
      int hw_idx;
      s = format (s, "Slave-Idx:");
      clib_bitmap_foreach (hw_idx, hi->bond_info, s =
			   format (s, " %d", hw_idx));
    }
  else if (dev_class->format_device_name)
    s = format (s, "%U", dev_class->format_device_name, hi->dev_instance);
  else
    s = format (s, "%s%d", dev_class->name, hi->dev_instance);

  if (verbose)
    {
      if (hw_class->format_device)
	s = format (s, "\n%U%U",
		    format_white_space, indent + 2,
		    hw_class->format_device, hi->hw_if_index, verbose);
      else
	{
	  s = format (s, "\n%U%s",
		      format_white_space, indent + 2, hw_class->name);
	  if (hw_class->format_address && vec_len (hi->hw_address) > 0)
	    s =
	      format (s, " address %U", hw_class->format_address,
		      hi->hw_address);
	}

      if (dev_class->format_device)
	s = format (s, "\n%U%U",
		    format_white_space, indent + 2,
		    dev_class->format_device, hi->dev_instance, verbose);
    }

  return s;
}

u8 *
format_vnet_sw_interface_name (u8 * s, va_list * args)
{
  vnet_main_t *vnm = va_arg (*args, vnet_main_t *);
  vnet_sw_interface_t *si = va_arg (*args, vnet_sw_interface_t *);
  vnet_sw_interface_t *si_sup =
    vnet_get_sup_sw_interface (vnm, si->sw_if_index);
  vnet_hw_interface_t *hi_sup;

  ASSERT (si_sup->type == VNET_SW_INTERFACE_TYPE_HARDWARE);
  hi_sup = vnet_get_hw_interface (vnm, si_sup->hw_if_index);

  s = format (s, "%v", hi_sup->name);

  if (si->type != VNET_SW_INTERFACE_TYPE_HARDWARE)
    s = format (s, ".%d", si->sub.id);

  return s;
}

u8 *
format_vnet_sw_if_index_name (u8 * s, va_list * args)
{
  vnet_main_t *vnm = va_arg (*args, vnet_main_t *);
  u32 sw_if_index = va_arg (*args, u32);
  vnet_sw_interface_t *si;

  si = vnet_get_sw_interface_safe (vnm, sw_if_index);

  if (NULL == si)
    {
      return format (s, "DELETED");
    }
  return format (s, "%U", format_vnet_sw_interface_name, vnm, si);
}

/* Unparse memory size e.g. 100, 100k, 100m, 100g. */
static u8 *
format_mbps_pps (u8 * s, va_list * va)
{
  f64 size = va_arg (*va, f64);
  uword l, u, log_u;
  uword usize = (uword) size;

  l = usize > 0 ? min_log2 (usize) : 0;
  if (l < 10)
    log_u = 0;
  else if (l < 20)
    log_u = 10;
  else if (l < 30)
    log_u = 20;
  else
    log_u = 30;

  u = (uword) 1 << log_u;
  s = format (s, "%+10.3f", (f64) size / (f64) u);

  if (log_u != 0)
    s = format (s, "%c", " KMG"[log_u / 10]);

  return s;
}


u8 *
format_vnet_sw_interface_cntrs (u8 * s, vnet_interface_main_t * im,
				vnet_sw_interface_t * si, u8 verbose)
{
  u32 indent, n_printed;
  int i, j, n_counters;
  static vnet_main_t **my_vnet_mains;

  vec_reset_length (my_vnet_mains);

  indent = format_get_indent (s);
  n_printed = 0;

  vnet_interface_counter_t *if_counter;

  if_counter = pool_elt_at_index (im->instant_if_counters, si->counter_index);  
  
  f64 this_time = vlib_time_now(vlib_get_main());
  f64 duration = this_time - if_counter->last_show_time;
  if_counter->last_show_time = this_time;	

  vlib_counter_t counter;
  f64 * packets_rate = 0;
  f64 * bytes_rate = 0;
	
  vlib_thread_main_t *tm = vlib_get_thread_main ();

  vec_validate (packets_rate, tm->n_vlib_mains - 1);
  vec_validate (bytes_rate, tm->n_vlib_mains - 1);
	
  {
    vlib_combined_counter_main_t *cm;
    vlib_counter_t v, vtotal;
    u8 *n = 0;

    for (i = 0; i < vec_len (vnet_mains); i++)
      {
	if (vnet_mains[i])
	  vec_add1 (my_vnet_mains, vnet_mains[i]);
      }

    if (vec_len (my_vnet_mains) == 0)
      vec_add1 (my_vnet_mains, &vnet_main);

    /* Each vnet_main_t has its own copy of the interface counters */
    n_counters = vec_len (im->combined_sw_if_counters);

    /* rx, tx counters... */
    for (j = 0; j < n_counters; j++)
      {
	vtotal.packets = 0;
	vtotal.bytes = 0;

	cm = im->combined_sw_if_counters + j;

	for (i = 0; i < vec_len (my_vnet_mains); i++)
	  {
	    im = &my_vnet_mains[i]->interface_main;
	    vlib_get_combined_counter (cm, si->sw_if_index, &v);
	    vtotal.packets += v.packets;
	    vtotal.bytes += v.bytes;

		vlib_counter_t * last = if_counter->combined_per_thread[j];

		for (int k = 0; k < vec_len (last); k ++)
		  {
		    vlib_get_combined_counter_per_thread (cm, si->sw_if_index, k, &v);

			vlib_counter_t * this_one = vec_elt_at_index (last, k);
			
			counter.packets = v.packets - this_one->packets;
			counter.bytes = v.bytes - this_one->bytes;
			this_one->packets = v.packets;
			this_one->bytes = v.bytes;
			
			packets_rate[k] = counter.packets / duration;
			bytes_rate[k] = counter.bytes / duration;
		  }
	  }

	/* Only display non-zero counters. */
	if (vtotal.packets == 0)
	  continue;

	vlib_counter_t * last = &if_counter->combined_total[j];

	counter.packets = vtotal.packets - last->packets;
	counter.bytes = vtotal.bytes - last->bytes;
	last->packets = vtotal.packets;
	last->bytes = vtotal.bytes;
	
	f64 packet_rate = counter.packets / duration;
	f64 byte_rate = counter.bytes / duration;

	if (n_printed > 0)
	  s = format (s, "\n%U", format_white_space, indent);
	n_printed += 2;

	if (n)
	  _vec_len (n) = 0;
	n = format (n, "%s packets", cm->name);
	s = format (s, "%-16v%16Ld %U pps", n, vtotal.packets, format_mbps_pps, packet_rate);

	if(verbose)
		for (i = 0; i < vec_len (packets_rate); i++)
	    {
	    	if(packets_rate[i] > 0)
					s = format (s, "\n%U Thread %u %-10v: %U", format_white_space, indent + 12, i, vlib_worker_threads[i].name, format_mbps_pps, packets_rate[i]);	      
		  }

	_vec_len (n) = 0;
	n = format (n, "%s bytes", cm->name);
	s = format (s, "\n%U%-16v%16Ld %U bps",
		    format_white_space, indent, n, vtotal.bytes, format_mbps_pps, byte_rate * 8);
	
	if(verbose)
		for (i = 0; i < vec_len (packets_rate); i++)
      {
				if(packets_rate[i] > 0)
					s = format (s, "\n%U Thread %u %-10v: %U", format_white_space, indent + 12, i, vlib_worker_threads[i].name, format_mbps_pps, bytes_rate[i] * 8);	      
		  }
    }
    vec_free (n);
  }

  {
    vlib_simple_counter_main_t *cm;
    u64 v, vtotal;

    n_counters = vec_len (im->sw_if_counters);

    for (j = 0; j < n_counters; j++)
      {
	vtotal = 0;

	for (i = 0; i < vec_len (my_vnet_mains); i++)
	  {
	    im = &my_vnet_mains[i]->interface_main;
	    cm = im->sw_if_counters + j;

	    v = vlib_get_simple_counter (cm, si->sw_if_index);
	    vtotal += v;

		  counter_t * last = if_counter->simple_per_thread[j];
		  
		  for (int k = 0; k < vec_len (last); k ++)
			{
			  v = vlib_get_simple_counter_per_thread (cm, si->sw_if_index, k);
		  
			  counter_t this_one = vec_elt (last, k);
			  
			  counter_t tmp = v - this_one;
			  vec_elt (last, k) = v;
			  
			  packets_rate[k] = tmp / duration;
			}
	  }

	/* Only display non-zero counters. */
	if (vtotal == 0)
	  continue;

	counter_t tmp;
	counter_t * last = &if_counter->simple_total[j];

	tmp = vtotal - *last;
	*last = vtotal;	
	f64 rate = tmp / duration;

	if (n_printed > 0)
	  s = format (s, "\n%U", format_white_space, indent);
	n_printed += 1;

	s = format (s, "%-16s%16Ld %U", cm->name, vtotal, format_mbps_pps, rate);
	if(verbose)
		for (i = 0; i < vec_len (packets_rate); i++)
      {
				if(packets_rate[i] > 0)
					s = format (s, "\n%U Thread %u %-10v: %U", format_white_space, indent + 12, i, vlib_worker_threads[i].name, format_mbps_pps, packets_rate[i]);	      
		  }
    }
  }

  vec_free (packets_rate);
  vec_free (bytes_rate);

  return s;
}

u8 *
format_vnet_sw_interface (u8 * s, va_list * args)
{
  vnet_main_t *vnm = va_arg (*args, vnet_main_t *);
  vnet_sw_interface_t *si = va_arg (*args, vnet_sw_interface_t *);
  vnet_interface_main_t *im = &vnm->interface_main;
  u8 verbose = va_arg (*args, u8 *);

  if (!si)
    return format (s, "%=32s%=8s%=16s%=16s%=16s%=16s",
		   "Name", "Idx", "State", "Counter", "Count", "Rate");

  s = format (s, "%-32U%=8d%=16U",
	      format_vnet_sw_interface_name, vnm, si, si->sw_if_index,
	      format_vnet_sw_interface_flags, si->flags);

  s = format_vnet_sw_interface_cntrs (s, im, si, verbose);

  return s;
}

u8 *
format_vnet_sw_interface_name_override (u8 * s, va_list * args)
{
  vnet_main_t *vnm = va_arg (*args, vnet_main_t *);
  vnet_sw_interface_t *si = va_arg (*args, vnet_sw_interface_t *);
  /* caller supplied display name for this interface */
  u8 *name = va_arg (*args, u8 *);
  vnet_interface_main_t *im = &vnm->interface_main;


  if (!si)
    return format (s, "%=32s%=5s%=16s%=16s%=16s",
		   "Name", "Idx", "State", "Counter", "Count");

  s = format (s, "%-32v%=5d%=16U",
	      name, si->sw_if_index,
	      format_vnet_sw_interface_flags, si->flags);

  s = format_vnet_sw_interface_cntrs (s, im, si, 0);

  return s;
}

uword
unformat_vnet_hw_interface (unformat_input_t * input, va_list * args)
{
  vnet_main_t *vnm = va_arg (*args, vnet_main_t *);
  u32 *hw_if_index = va_arg (*args, u32 *);
  vnet_interface_main_t *im = &vnm->interface_main;
  vnet_device_class_t *c;

  /* Try per device class functions first. */
  vec_foreach (c, im->device_classes)
  {
    if (c->unformat_device_name
	&& unformat_user (input, c->unformat_device_name, hw_if_index))
      return 1;
  }

  return unformat_user (input, unformat_hash_vec_string,
			im->hw_interface_by_name, hw_if_index);
}

uword
unformat_vnet_sw_interface (unformat_input_t * input, va_list * args)
{
  vnet_main_t *vnm = va_arg (*args, vnet_main_t *);
  u32 *result = va_arg (*args, u32 *);
  vnet_hw_interface_t *hi;
  u32 hw_if_index, id, id_specified;
  u32 sw_if_index;
  u8 *if_name = 0;
  uword *p, error = 0;

  id = ~0;
  if (unformat (input, "%_%v.%d%_", &if_name, &id)
      && ((p = hash_get (vnm->interface_main.hw_interface_by_name, if_name))))
    {
      hw_if_index = p[0];
      id_specified = 1;
    }
  else
    if (unformat (input, "%U", unformat_vnet_hw_interface, vnm, &hw_if_index))
    id_specified = 0;
  else
    goto done;

  hi = vnet_get_hw_interface (vnm, hw_if_index);
  if (!id_specified)
    {
      sw_if_index = hi->sw_if_index;
    }
  else
    {
      if (!(p = hash_get (hi->sub_interface_sw_if_index_by_id, id)))
	goto done;
      sw_if_index = p[0];
    }
  if (!vnet_sw_interface_is_api_visible (vnm, sw_if_index))
    goto done;
  *result = sw_if_index;
  error = 1;
done:
  vec_free (if_name);
  return error;
}

uword
unformat_vnet_sw_interface_flags (unformat_input_t * input, va_list * args)
{
  u32 *result = va_arg (*args, u32 *);
  u32 flags = 0;

  if (unformat (input, "up"))
    flags |= VNET_SW_INTERFACE_FLAG_ADMIN_UP;
  else if (unformat (input, "down"))
    flags &= ~VNET_SW_INTERFACE_FLAG_ADMIN_UP;
  else if (unformat (input, "punt"))
    flags |= VNET_SW_INTERFACE_FLAG_PUNT;
  else if (unformat (input, "enable"))
    flags &= ~VNET_SW_INTERFACE_FLAG_PUNT;
  else
    return 0;

  *result = flags;
  return 1;
}

uword
unformat_vnet_hw_interface_flags (unformat_input_t * input, va_list * args)
{
  u32 *result = va_arg (*args, u32 *);
  u32 flags = 0;

  if (unformat (input, "up"))
    flags |= VNET_HW_INTERFACE_FLAG_LINK_UP;
  else if (unformat (input, "down"))
    flags &= ~VNET_HW_INTERFACE_FLAG_LINK_UP;
  else
    return 0;

  *result = flags;
  return 1;
}

/*
 * fd.io coding-style-patch-verification: ON
 *
 * Local Variables:
 * eval: (c-set-style "gnu")
 * End:
 */
