#include <vnet/fib/ip4_fib.h>
#include <vnet/ip/ip.h>
#include <vnet/ethernet/ethernet.h>
#include <svm/ssvm.h>
#include <vlibmemory/socket_client.h>
#include <vlibapi/vat_helper_macros.h>
#include <vat/vat.h>
#include <gtpu/gtpu.h>
#include <gtpu/gtpu_msg_enum.h>
#include <vnet/lisp-cp/packets.h>

/* define message structures */
#define vl_typedefs
#include <gtpu/gtpu.api.h>
#undef vl_typedefs

vlib_node_registration_t gtpu_process_node;
vlib_node_registration_t gtpu_echo_node;

#define GTPU_TIMEOUT_TIME          20  /* 30 s */
#define GTPU_ECHO_INTERVAL         20  /* 30 s */
#define GTPU_RETRANSMIT_COUNT      3
#define GTPU_RETRANSMIT_INTERVAL   3   /* 3s */

#define foreach_ip4_offset  _(0) _(1) _(2) _(3)
#define foreach_ip6_offset  \
    _(0) _(1) _(2) _(3) _(4) _(5) _(6) _(7) _(8) _(9) _(10) _(11) _(12) _(13) _(14) _(15)


static void 
gtpu_path_info_notify(u8 event_type, u32 teid, ip46_address_t *dst)
{
    gtpu_main_t *gtm = &gtpu_main;
    vl_api_registration_t *vl_reg;
    vl_api_gtpu_error_indication_details_t *mp;
    vpe_client_registration_t *client;
    gtpu_client_registration_t *registrations = &gtm->registrations;

    mp = vl_msg_api_alloc_as_if_client (sizeof (*mp));
    memset (mp, 0, sizeof (*mp));

    mp->_vl_msg_id = ntohs (VL_API_GTPU_ERROR_INDICATION_DETAILS);

    mp->teid = teid;
    mp->code = event_type;
    
#define _(offs) mp->dst_address[offs] = dst->as_u8[offs];
    if (ip46_address_is_ip4 (dst))
    {
        foreach_ip4_offset
    }
    else
    {
        foreach_ip6_offset
    }
#undef _

#if 0
    /* path error */
    if (!teid)
    {
        mp_path = vl_msg_api_alloc_as_if_client (sizeof (*mp_path));
        memset (mp_path, 0, sizeof (*mp_path));

        mp_path->_vl_msg_id = ntohs (VL_API_GTPU_PATH_ERROR_DETAILS);
        
#define _(offs) mp_path->dst_address[offs] = dst->as_u8[offs];
        if (ip46_address_is_ip4 (dst))
        {
            foreach_ip4_offset
        }
        else
        {
            foreach_ip6_offset
        }
#undef _

        mp = (u8 *)mp_path;
    }
    /* tunnel error */
    else
    {
        mp_tunnel = vl_msg_api_alloc_as_if_client (sizeof (*mp_tunnel));
        memset (mp_tunnel, 0, sizeof (*mp_tunnel));

        mp_tunnel->_vl_msg_id = ntohs (VL_API_GTPU_TUNNEL_ERROR_DETAILS);
        mp_tunnel->teid = teid;
            
#define _(offs) mp_tunnel->dst_address[offs] = dst->as_u8[offs];
        if (ip46_address_is_ip4 (dst))
        {
            foreach_ip4_offset
        }
        else
        {
            foreach_ip6_offset
        }
#undef _

        mp = (u8 *)mp_tunnel;
    }
#endif
    /* *INDENT-OFF* */
    pool_foreach (client, registrations->clients, (
        		    {
                        vl_reg = vl_api_client_index_to_registration (client->client_index);

                        vl_api_send_msg (vl_reg, (u8 *)mp);
        		    }
                ));
    /* *INDENT-ON* */
}


static u8 
gtpu_path_timeout_check(vlib_main_t *vm, gtpu_path_t *path)
{
    if ((GTPU_TIMEOUT_TIME + path->last_receive_response_time) < path->last_send_request_time)
    {
        /* path error */
        if (path->retransmit > GTPU_RETRANSMIT_COUNT)
        {
            if (!path->has_notified)
            {
                gtpu_path_info_notify(GTPU_EVENT_PATH_ERROR, 0, &path->dst);
                path->has_notified = 1;
            }
            return 1;
        }
        /* retransmit */
        else if (!path->retransmit)
        {
            clib_warning ("Retransmit because timeout.");
            path->retransmit = 1;
        }
    }

    return 0;
}


static void 
gtpu_echo_request_send(vlib_main_t *vm, gtpu_path_t *path)
{
    ip4_header_t *ip4;
    ip6_header_t *ip6;
    udp_header_t *udp;
    gtpu_header_t *gtpu;
    u32 buffer_id = 0;
    vlib_buffer_t *buffer;
    vlib_frame_t *frame;
    u32 *to_next;
    vlib_buffer_free_list_t *fl;
    u8 is_ip4 = 0;
    
    if (vlib_buffer_alloc (vm, &buffer_id, 1) != 1)
    {
        clib_warning ("BUG: Alloc echo request buffer failed");
        return;
    }

    buffer = vlib_get_buffer (vm, buffer_id);
    fl = vlib_buffer_get_free_list (vm, VLIB_BUFFER_DEFAULT_FREE_LIST_INDEX);
    vlib_buffer_init_for_free_list (buffer, fl);
    VLIB_BUFFER_TRACE_TRAJECTORY_INIT (buffer);

    /* Fix ip header */
    if (ip46_address_is_ip4 (&path->src)) /* ip4 */
    {
        ip4 = vlib_buffer_get_current (buffer);
        ip4->ip_version_and_header_length = 0x45;
        ip4->ttl = 254;
        ip4->protocol = IP_PROTOCOL_UDP;
        ip4->src_address = path->src.ip4;
        ip4->dst_address = path->dst.ip4;
        ip4->length = clib_host_to_net_u16(sizeof(*ip4) + sizeof(*udp) + (sizeof(*gtpu) - 4)/* Now only support 8-byte gtpu header. TBD */);
        ip4->checksum = ip4_header_checksum (ip4);

        buffer->current_length = sizeof(*ip4) + sizeof(*udp) + (sizeof(*gtpu) - 4);/* Now only support 8-byte gtpu header. TBD */

        udp = (udp_header_t *)(ip4 + 1);
        udp->src_port = clib_host_to_net_u16 (GTPU_UDP_SRC_DEFAULT_PORT);
        udp->dst_port = clib_host_to_net_u16 (GTPU_UDP_DST_PORT);

        is_ip4 = 1;
    }
    else /* ip6 */
    {
        ip6 = vlib_buffer_get_current (buffer);
        ip6->ip_version_traffic_class_and_flow_label = clib_host_to_net_u32 (6 << 28);
        ip6->hop_limit = 255;
        ip6->protocol = IP_PROTOCOL_UDP;
        ip6->src_address = path->src.ip6;
        ip6->dst_address = path->dst.ip6;
        ip6->payload_length = clib_host_to_net_u16(sizeof(*udp) + (sizeof(*gtpu) - 4)/* Now only support 8-byte gtpu header. TBD */);

        buffer->current_length = sizeof(*ip6) + sizeof(*udp) + (sizeof(*gtpu) - 4);/* Now only support 8-byte gtpu header. TBD */
        
        udp = (udp_header_t *)(ip6 + 1);
        udp->src_port = clib_host_to_net_u16 (GTPU6_UDP_SRC_DEFAULT_PORT);
        udp->dst_port = clib_host_to_net_u16 (GTPU6_UDP_DST_PORT);
    }

    /* Fix UDP length */
    udp->length = clib_host_to_net_u16(sizeof(*udp) + (sizeof(*gtpu) - 4)/* Now only support 8-byte gtpu header. TBD */);
    
    /* Fix GTPU */
    gtpu = (gtpu_header_t *)(udp + 1);
    gtpu->ver_flags = GTPU_V1_VER | GTPU_PT_GTP;
    gtpu->type = GTPU_TYPE_ECHO_REQUEST;   /* set the message type with echo request */
    gtpu->teid = 0;                       /* the teid of echo request packets must be 0 */
    gtpu->length = clib_host_to_net_u16((sizeof(*gtpu) - 4)/* Now only support 8-byte gtpu header. TBD */);
    
    /* Enqueue the packet right now */
    if (is_ip4) /* ip4 */
    {
        frame = vlib_get_frame_to_node (vm, ip4_lookup_node.index);
        to_next = vlib_frame_vector_args (frame);
        to_next[0] = buffer_id;
        frame->n_vectors = 1;
        vlib_put_frame_to_node (vm, ip4_lookup_node.index, frame);
    }
    else  /* ip6 */
    {
        frame = vlib_get_frame_to_node (vm, ip6_lookup_node.index);
        to_next = vlib_frame_vector_args (frame);
        to_next[0] = buffer_id;
        frame->n_vectors = 1;
        vlib_put_frame_to_node (vm, ip6_lookup_node.index, frame);
    }
}


static u8 gtpu_echo_request_check(vlib_main_t *vm, gtpu_path_t *path)
{
    f64 now = vlib_time_now (vm);

    if ((path->retransmit && (GTPU_RETRANSMIT_INTERVAL <= now - path->last_send_request_time))
        || (!path->retransmit && (GTPU_ECHO_INTERVAL <= now - path->last_send_request_time)))
    {
        path->transmit = 1;
        path->retransmit = path->retransmit ? path->retransmit + 1 : 0;
        return 1;
    }

    return 0;
}

static void gtpu_event_process(vlib_main_t * vm, uword event_type, uword *event_data)
{
    gtpu_main_t *gtm = &gtpu_main;
    u32 bi;
    vlib_buffer_t *buffer;
    ip4_header_t *ip4;
    ip6_header_t *ip6;
    gtpu4_tunnel_key_t key4;
    gtpu6_tunnel_key_t key6;
    uword *p;
    gtpu_path_t *path;
    gtpu_header_t * gtpu;

    if (!event_data)
    {
        return ;
    }
    bi = *event_data;
    
    switch (event_type)
    {
        case GTPU_EVENT_TYPE_ECHO_RESPONSE_IP4:
        case GTPU_EVENT_TYPE_ECHO_RESPONSE_IP6:
            {
                buffer = vlib_get_buffer (vm, bi);
                
                if (GTPU_EVENT_TYPE_ECHO_RESPONSE_IP4 == event_type) /* ip4 */
                {
                    ip4 = vlib_buffer_get_current(buffer);
                    key4.src = ip4->src_address.as_u32;
                    key4.teid = 0;
                    
                    p = hash_get (gtm->path_manage.gtpu4_path_by_key, key4.as_u64);
                }
                else        /* ip6 */
                {
                    ip6 = vlib_buffer_get_current (buffer);
                    key6.src.as_u64[0] = ip6->src_address.as_u64[0];
                    key6.src.as_u64[1] = ip6->src_address.as_u64[1];
                    key6.teid = 0;

                    p = hash_get_mem (gtm->path_manage.gtpu6_path_by_key, &key6);
                }
                
                if (!p)
                {
                    clib_warning ("BUG: Has no this path");
                    goto out;
                }

                path = pool_elt_at_index (gtm->path_manage.paths, p[0]);
                path->last_receive_response_time = vlib_time_now (vm);
                path->retransmit = 0;
                path->has_notified = 0;
            }
            break;
        case GTPU_EVENT_TYPE_ERROR_INDICATE_IP4:
        case GTPU_EVENT_TYPE_ERROR_INDICATE_IP6:
        case GTPU_EVENT_TYPE_NO_SUCH_TUNNEL_IP4:
        case GTPU_EVENT_TYPE_NO_SUCH_TUNNEL_IP6:
        case GTPU_EVENT_TYPE_VERSION_NOT_SUPPORTED_IP4:
        case GTPU_EVENT_TYPE_VERSION_NOT_SUPPORTED_IP6:
            {
                u8 error = 0;
                ip46_address_t dst;
                
                buffer = vlib_get_buffer (vm, bi);
                gtpu = vlib_buffer_get_current (buffer);
                
                if (GTPU_EVENT_TYPE_ERROR_INDICATE_IP4 == event_type
                    || GTPU_EVENT_TYPE_NO_SUCH_TUNNEL_IP4 == event_type
                    || GTPU_EVENT_TYPE_VERSION_NOT_SUPPORTED_IP4 == event_type) /* ip4 */
                {
                    ip4 = vlib_buffer_get_current(buffer);                    
                    dst = to_ip46(0, ip4->src_address.as_u8);
                    error = (GTPU_EVENT_TYPE_ERROR_INDICATE_IP4 == event_type ? GTPU_EVENT_RECEIVE_ERROR_INDICATION : 
                                (GTPU_EVENT_TYPE_NO_SUCH_TUNNEL_IP4 == event_type ? GTPU_EVENT_NO_SUCH_TUNNEL : GTPU_EVENT_VERSION_NOT_SUPPORTED));
                }
                else /* ip6 */
                {
                    ip6 = vlib_buffer_get_current (buffer);
                    dst = to_ip46(1, ip6->src_address.as_u8);
                    error = (GTPU_EVENT_TYPE_ERROR_INDICATE_IP6 == event_type ? GTPU_EVENT_RECEIVE_ERROR_INDICATION : 
                                (GTPU_EVENT_TYPE_NO_SUCH_TUNNEL_IP6 == event_type ? GTPU_EVENT_NO_SUCH_TUNNEL : GTPU_EVENT_VERSION_NOT_SUPPORTED));
                }
                
                gtpu_path_info_notify(error, gtpu->teid, &dst);
            }
            break;
        default:
            clib_warning ("BUG: Unknow event type 0x%wx", event_type);
            break;
    }

out:
    vlib_buffer_free_one(vm, bi);
    return;
}

static uword
gtpu_process (vlib_main_t * vm, vlib_node_runtime_t * node, vlib_frame_t * f)
{
    gtpu_main_t *gtm = &gtpu_main;
    gtpu_path_t *path;
    uword event_type, *event_data = 0;
    u8 echo = 0;
    
    while (1)
    {
        /* gtpu process run interval is 1 second */
        vlib_process_wait_for_event_or_clock(vm, 1);
        event_type = vlib_process_get_events (vm, &event_data);
        switch (event_type)
        {
            case ~0:		/* timeout */
              break;
            
            default:        /* event */
              gtpu_event_process(vm, event_type, event_data);
              if (event_data)
              {
                  vec_free (event_data);
              }
              continue;
        }
        
        pool_foreach (path, gtm->path_manage.paths, (
        		   {
        		       if (!gtpu_path_timeout_check(vm, path))
                       {
            		       echo += gtpu_echo_request_check(vm, path);
                       }      
        		   }
                ));

        if (echo)
        {
            vlib_main_t *work_vm = vm;
            if (vlib_num_workers ())
                work_vm = vlib_get_worker_vlib_main (0);
            
            vlib_node_set_state (work_vm, gtpu_echo_node.index, VLIB_NODE_STATE_POLLING);
            echo = 0;
        }
    }

    return 0;
}


/* *INDENT-OFF* */
VLIB_REGISTER_NODE (gtpu_process_node) =
{
  .function = gtpu_process,
  .type = VLIB_NODE_TYPE_PROCESS,
  .name = "gtpu_process",
};
/* *INDENT-ON* */


static uword
gtpu_echo_input (vlib_main_t * vm, vlib_node_runtime_t * node, vlib_frame_t * f)
{
    gtpu_main_t *gtm = &gtpu_main;
    gtpu_path_t *path;

    pool_foreach (path, gtm->path_manage.paths, (
		   {
		       if (path->transmit)
               {
    		       gtpu_echo_request_send(vm, path);
                   path->last_send_request_time = vlib_time_now(vm);
                   path->transmit = 0;
               }
		   }
        ));

    vlib_node_set_state (vm, node->node_index, VLIB_NODE_STATE_DISABLED);

    return 0;
}


/* *INDENT-OFF* */
VLIB_REGISTER_NODE (gtpu_echo_node) =
{
  .function = gtpu_echo_input,
  .type = VLIB_NODE_TYPE_INPUT,
  .name = "gtpu_echo_input",
  /* Node will be left disabled until need to send echo request packets. */
  .state = VLIB_NODE_STATE_DISABLED,
};
/* *INDENT-ON* */



