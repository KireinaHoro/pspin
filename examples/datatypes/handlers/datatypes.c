// Copyright 2020 ETH Zurich
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pspin.h"
#include <handler.h>
#include <packets.h>
#include <spin_dma.h>
#include <spin_host.h>

#include "mpitypes_dataloop.h"

#include "datatype_descr.h"
#include "datatypes.h"
#include "mpit_offloaded.h"
#include "pspin_rt.h"
#include "spin_conf.h"

#define SIZE_MSG (128 * 1024 * 1024) // 128 MB
#define MSG_PAGES (SIZE_MSG / PAGE_SIZE)

#define DEBUG(...) printf(__VA_ARGS__)

// perf counter layout:
// 0: per-packet cycles
// 1: per-message cycles
// TODO: more fine-grained counters?

// FIXME: this would break if we have interleaving messages
static volatile uint32_t msg_start;

__handler__ void datatypes_hh(handler_args_t *args) {
  DEBUG("Start of message (flow_id %d)\n", args->task->flow_id);
  msg_start = cycles();
}

__handler__ void datatypes_th(handler_args_t *args) {
  DEBUG("End of message (flow_id %d)\n", args->task->flow_id);

  // counter 1: message average time
  // FIXME: should this include the notification time?
  push_counter(&__host_data.counters[1], cycles() - msg_start);

  // notify host for unpacked message -- 0-byte host request
  // FIXME: this is synchronous; do we need an asynchronous interface?
  fpspin_host_req(args, 0);
}

static inline bool check_host_mem(handler_args_t *args) {
  // extra MSG_PAGES pages after the messaging pages
  return HOST_ADDR(args) &&
         args->task->host_mem_size >= PAGE_SIZE * (MSG_PAGES + CORE_COUNT);
}

__handler__ void datatypes_ph(handler_args_t *args) {
  printf("Packet (size %d) flow_id %d\n", args->task->pkt_mem_size,
         args->task->flow_id);

  task_t *task = args->task;

  uint32_t coreid = args->hpu_gid;
  spin_datatype_mem_t *dtmem = (spin_datatype_mem_t *)task->handler_mem;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint8_t *slmp_pld = (uint8_t *)(task->pkt_mem) + sizeof(slmp_pkt_hdr_t);
  uint16_t slmp_pld_len = SLMP_PAYLOAD_LEN(hdrs);
  uint32_t stream_start_offset = hdrs->slmp_hdr.pkt_off;
  uint32_t stream_end_offset = stream_start_offset + slmp_pld_len;

  dtmem->state[coreid].params.streambuf = (void *)slmp_pld;
  // first CORE_COUNT pages are for the req/resp interface
  dtmem->state[coreid].params.userbuf =
      HOST_ADDR(args) + CORE_COUNT * PAGE_SIZE;
  uint64_t last = stream_end_offset;

  // hang if host memory not defined
  if (!check_host_mem(args)) {
    printf("FATAL: host memory size not enough\n");
    for (;;)
      ;
  }

  uint32_t start = cycles();

  spin_segment_manipulate(&dtmem->state[coreid].state, stream_start_offset,
                          &last, &dtmem->state[coreid].params);

  uint32_t end = cycles();

  // counter 0: per-packet average time
  push_counter(&__host_data.counters[0], end - start);
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {datatypes_hh, datatypes_ph, datatypes_th};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];
}
