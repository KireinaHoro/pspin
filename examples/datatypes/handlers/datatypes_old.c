#include "nic_thread.h"
#include <stdio.h>
#include <assert.h>

#include "mpitypes_dataloop.h"
#include "datatype_descr.h"
#include "datatypes.h"
#include "mpit_offloaded.h"


int datatypes_hh(const handler_args_t *args)
{
	return 0;
}

int datatypes_ph(const handler_args_t *args)
{
	uint32_t coreid = args->hid;
	spin_datatype_mem_t *dtmem = (spin_datatype_mem_t *)args->handler_mem;

	long unsigned int last;

	//WARNING: enable this!!!
	unsigned int stream_start_offset = args->pkt_offset;
	unsigned int stream_end_offset = args->pkt_offset + args->pkt_pld_len;

	dtmem->state[coreid].params.streambuf = (void *)args->pkt_pld; //FIXME
	dtmem->state[coreid].params.userbuf = (char *)args->host_addr;
	last = stream_end_offset;

#ifdef DDT_TRACE
    ptl_epu_trace(); 
#endif

	spin_segment_manipulate(&(dtmem->state[coreid].state), stream_start_offset, &last, &(dtmem->state[coreid].params));

#ifdef DDT_TRACE
    ptl_epu_trace(); 
#endif

	return PTL_CONTINUE;
}

int datatypes_ph_fp(const handler_args_t *args)
{
	uint32_t coreid = args->hid;
	spin_datatype_mem_t *dtmem = (spin_datatype_mem_t *)args->handler_mem;

	long unsigned int last;

	//WARNING: enable this!!!
	unsigned int stream_start_offset = args->pkt_offset;
	unsigned int stream_end_offset = args->pkt_offset + args->pkt_pld_len;

	dtmem->state[coreid].params.streambuf = (void *)args->pkt_pld; //FIXME
	dtmem->state[coreid].params.userbuf = (char *)args->host_addr;
	last = stream_end_offset;

#ifdef DDT_TRACE
    ptl_epu_trace(); 
#endif

	spin_segment_manipulate_fp(&(dtmem->state[coreid].state), stream_start_offset, &last, &(dtmem->state[coreid].params));

#ifdef DDT_TRACE
    ptl_epu_trace(); 
#endif

	return PTL_CONTINUE;
}

int datatypes_th(const handler_args_t *args)
{
	//char * str = (char *) args->handler_mem;
	//printf("This is what the handlers found: %s\n", str);

	//TODO: here we need to make a write to generate the event.
	//      it should be a 0-bytes write (if possible) or it should just
	//      write one byte in the ME memory that does not overlap with the
	//      data. Now I just write on the first byte because we don't care
	//      about the actual data for now.
	ptl_epu_dma_to_host(args->pkt_pld, (uint64_t)args->host_addr, 0, 1);

	return PTL_CONTINUE;
}

int main(int argc, char **argv)
{

	nic_thread_init();

	handler_t handlers[4];
	handlers[0] = datatypes_hh;
	handlers[1] = datatypes_ph;
	handlers[2] = datatypes_th;
	handlers[3] = datatypes_ph_fp;

	nic_thread_set_handlers(handlers, 4);

	return nic_thread_main();
}
