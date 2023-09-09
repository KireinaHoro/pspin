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

#include "mpitypes_dataloop_pspin.h"

#include "datatype_descr.h"
#include "datatypes.h"
#include "mpit_offloaded.h"
#include "pspin_rt.h"
#include "spin_conf.h"

#define SIZE_MSG (8 * 1024 * 1024) // 8 MB
#define MSG_PAGES (SIZE_MSG / PAGE_SIZE) * CORE_COUNT

// #define DATATYPES_DEBUG

#ifdef DATATYPES_DEBUG
#define DEBUG(...)                                                             \
  do {                                                                         \
    printf(__VA_ARGS__);                                                       \
  } while (0)
#else
#define DEBUG(...)
#endif

#define FROM_L1

#ifdef FROM_L1
#define PKT_MEM(task) ((task)->pkt_mem)
#else
#define PKT_MEM(task) ((task)->l2_pkt_mem)
#endif

// perf counter layout:
// 0: per-packet cycles
// 1: per-message cycles
// TODO: more fine-grained counters?

#define SWAP(a, b, type)                                                       \
  do {                                                                         \
    type tmp = a;                                                              \
    a = b;                                                                     \
    b = tmp;                                                                   \
  } while (0)

static void prepare_hdrs_ack(slmp_pkt_hdr_t *hdrs, uint16_t flags) {
  SWAP(hdrs->udp_hdr.dst_port, hdrs->udp_hdr.src_port, uint16_t);
  SWAP(hdrs->ip_hdr.dest_id, hdrs->ip_hdr.source_id, uint32_t);
  SWAP(hdrs->eth_hdr.dest, hdrs->eth_hdr.src, mac_addr_t);

  hdrs->ip_hdr.length =
      htons(sizeof(ip_hdr_t) + sizeof(udp_hdr_t) + sizeof(slmp_hdr_t));
  hdrs->ip_hdr.checksum = 0;
  hdrs->ip_hdr.checksum =
      ip_checksum((uint8_t *)&hdrs->ip_hdr, sizeof(ip_hdr_t));
  hdrs->udp_hdr.length = htons(
      sizeof(slmp_hdr_t) + sizeof(udp_hdr_t)); // only SLMP header as payload
  hdrs->udp_hdr.checksum = 0;
  hdrs->slmp_hdr.flags = htons(flags);
}

static void send_ack(slmp_pkt_hdr_t *hdrs, task_t *task) {
  prepare_hdrs_ack(hdrs, MKACK);

  spin_cmd_t put;
  spin_send_packet(PKT_MEM(task), sizeof(slmp_pkt_hdr_t), &put);
}

__handler__ void datatypes_hh(handler_args_t *args) {
  task_t *task = args->task;
  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(PKT_MEM(task));
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);
  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  spin_datatype_mem_t *dtmem = (spin_datatype_mem_t *)task->handler_mem;

  int flowid = args->task->flow_id;
  spin_core_state_t *my_state = &dtmem->state[flowid];
  DEBUG("Start of message #%u\n", flowid);

  // my_state->msg_start = cycles();

  if (!SYN(flags)) {
    printf("Error: first packet did not require SYN; flags = %#x\n", flags);
    for (;;)
      ;
  }
}

__handler__ void datatypes_th(handler_args_t *args) {
  task_t *task = args->task;
  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(PKT_MEM(task));
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);
  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  // counter 1: message average time
  // FIXME: should this include the notification time?
  int flowid = args->task->flow_id;
  spin_datatype_mem_t *dtmem = (spin_datatype_mem_t *)task->handler_mem;
  spin_core_state_t *my_state = &dtmem->state[flowid];
  // push_counter(&__host_data.counters[1], cycles() - my_state->msg_start);

  DEBUG("End of message #%u\n", flowid);

  if (!EOM(flags)) {
    printf("Error: last packet did not have EOM; flags = %#x\n", flags);
    for (;;)
      ;
  }

  // notify host for unpacked message -- 0-byte host request
  // FIXME: this is synchronous; do we need an asynchronous interface?
  // repurposed for msgid for diagnostics
  fpspin_host_req(args, flowid);
}

static inline bool check_host_mem(handler_args_t *args) {
  // extra MSG_PAGES pages after the messaging pages
  return HOST_ADDR(args) &&
         args->task->host_mem_size >= PAGE_SIZE * (MSG_PAGES + CORE_COUNT);
}

__handler__ void datatypes_ph(handler_args_t *args) {
  // TOOD: disable assertions in DDT
  // TODO: disable prints

  task_t *task = args->task;

  spin_datatype_mem_t *dtmem = (spin_datatype_mem_t *)task->handler_mem;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(PKT_MEM(task));
  uint8_t *slmp_pld = (uint8_t *)(PKT_MEM(task)) + sizeof(slmp_pkt_hdr_t);
  uint16_t slmp_pld_len = SLMP_PAYLOAD_LEN(hdrs);

  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);

  int flowid = args->task->flow_id;

  uint32_t stream_start_offset = pkt_off;
  uint32_t stream_end_offset = stream_start_offset + slmp_pld_len;

  spin_core_state_t *my_state = &dtmem->state[flowid];
  my_state->params.streambuf = (void *)slmp_pld;
  // first CORE_COUNT pages are for the req/resp interface
  my_state->params.userbuf =
      HOST_ADDR(args) + CORE_COUNT * PAGE_SIZE + flowid * SIZE_MSG;
  // FIXME: should check that userbuf writes do not exceed MSG_PAGES

  uint64_t last = stream_end_offset;

  // int time = cycles();
  DEBUG("[%d] P: [%d:%d] #%d @ %p\n", PKT_MEM(task),
        stream_start_offset, stream_end_offset, flowid, my_state);

// hang if host memory not defined
#ifdef DATATYPES_DEBUG
  if (!check_host_mem(args)) {
    printf("FATAL: host memory size not enough\n");
    for (;;)
      ;
  }
#endif

  // uint32_t start = cycles();

  spin_segment_manipulate(&my_state->state, stream_start_offset, &last,
                          &my_state->params);

  // uint32_t end = cycles();

  // send back ack, if the remote requests for it
  if (SYN(flags)) {
    send_ack(hdrs, task);
  }

  // counter 0: per-packet average time
  // push_counter(&__host_data.counters[0], end - start);
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {datatypes_hh, datatypes_ph, datatypes_th};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];
}
