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

#define SIZE_MSG (128 * 1024 * 1024) // 128 MB
#define MSG_PAGES (SIZE_MSG / PAGE_SIZE)

#if 1
#define DEBUG(...) printf(__VA_ARGS__)
#else
#define DEBUG(...)
#endif

#ifdef USE_SPIN_SPINLOCK
spin_lock_t spinlock;
#else
volatile uint32_t lock_owner;
static inline void lock(handler_args_t *args) {
  int res;
  int counter = 0;
  do {
    res = compare_and_swap(&lock_owner, 0, HPU_ID(args) + 1);
    if (res) {
      rt_time_wait_cycles(50);
      ++counter;
      if (!(counter % 100000)) {
        printf("Trying to lock...\n");
        printf("Owner: %d @ %p\n", lock_owner, &lock_owner);
      }
    }
  } while (res);
}
static inline void unlock() { amo_store(&lock_owner, 0); }

#endif
// lock-protected
static spin_msg_state_t *msg_states = NULL;

// perf counter layout:
// 0: per-packet cycles
// 1: per-message cycles
// TODO: more fine-grained counters?

// FIXME: this would break if we have interleaving messages
#if 0
uint32_t msg_start[CORE_COUNT];
#endif

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
  spin_send_packet(task->pkt_mem, sizeof(slmp_pkt_hdr_t), &put);
}

__handler__ void datatypes_hh(handler_args_t *args) {
  task_t *task = args->task;
  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);
  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  uint32_t coreid = args->hpu_gid;
  spin_datatype_mem_t *dtmem = (spin_datatype_mem_t *)task->handler_mem;

  int msgid = hdrs->slmp_hdr.msg_id;
  DEBUG("Start of message #%u (flow_id %d)\n", msgid, args->task->flow_id);

  // alloc message state
#ifndef USE_SPIN_SPINLOCK
  lock(args);
#else
  spin_lock_lock(&spinlock);
#endif
  INTEGRITY_CHECK();

  spin_msg_state_t *my_state = umm_malloc(sizeof(spin_msg_state_t));
  my_state->msgid = msgid;
  HASH_ADD_INT(msg_states, msgid, my_state);

  INTEGRITY_CHECK();
  POISON_CHECK();
#ifndef USE_SPIN_SPINLOCK
  unlock();
#else
  spin_lock_unlock(&spinlock);
#endif

  printf("Allocated message state @ %p\n", my_state);

  my_state->seg = dtmem->state[coreid].state;
  my_state->params = (struct MPIT_m2m_params){
      .direction = dtmem->state[coreid].params.direction};

#if 0
  msg_start[msgid % args->hpu_gid] = cycles();
#endif

  if (!SYN(flags)) {
    printf("Error: first packet did not require SYN; flags = %#x\n", flags);
    return;
  }
}

__handler__ void datatypes_th(handler_args_t *args) {
  task_t *task = args->task;
  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);
  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  // counter 1: message average time
  // FIXME: should this include the notification time?
  int msgid = hdrs->slmp_hdr.msg_id;
#if 0
  push_counter(&__host_data.counters[1],
               cycles() - msg_start[msgid % args->hpu_gid]);
#endif

  DEBUG("End of message #%u (flow_id %d)\n", msgid, args->task->flow_id);

  if (!EOM(flags)) {
    printf("Error: last packet did not have EOM; flags = %#x\n", flags);
    return;
  }

  spin_msg_state_t *my_state;
  HASH_FIND_INT(msg_states, &msgid, my_state);

#ifndef USE_SPIN_SPINLOCK
  lock(args);
#else
  spin_lock_lock(&spinlock);
#endif
  // deallocate message state
  HASH_DEL(msg_states, my_state);

  INTEGRITY_CHECK();

  // FIXME: need bitmap to handle out-of-order free
  umm_free(my_state);

  umm_integrity_check();
  umm_poison_check();

#ifndef USE_SPIN_SPINLOCK
  unlock();
#else
  spin_lock_unlock(&spinlock);
#endif

  // notify host for unpacked message -- 0-byte host request
  // FIXME: this is synchronous; do we need an asynchronous interface?
  // repurposed for msgid for diagnostics
  fpspin_host_req(args, msgid);
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

  uint32_t coreid = args->hpu_gid;
  spin_datatype_mem_t *dtmem = (spin_datatype_mem_t *)task->handler_mem;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint8_t *slmp_pld = (uint8_t *)(task->pkt_mem) + sizeof(slmp_pkt_hdr_t);
  uint16_t slmp_pld_len = SLMP_PAYLOAD_LEN(hdrs);

  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);

  int msgid = hdrs->slmp_hdr.msg_id;

  DEBUG("Packet: offset %d, pld size %d, flow_id %d\n", pkt_off, slmp_pld_len,
        args->task->flow_id);

  uint32_t stream_start_offset = pkt_off;
  uint32_t stream_end_offset = stream_start_offset + slmp_pld_len;

  spin_msg_state_t *my_state;
  HASH_FIND_INT(msg_states, &msgid, my_state);
  if (!my_state) {
    printf("FATAL: cannot find msgid=%u in hashtable\n", msgid);
    for (;;)
      ;
  }
  printf("my_state=%p &mpit_segment=%p\n", my_state, &my_state->seg);

  my_state->params.streambuf = (void *)slmp_pld;
  // first CORE_COUNT pages are for the req/resp interface
  my_state->params.userbuf = HOST_ADDR(args) + CORE_COUNT * PAGE_SIZE;
  uint64_t last = stream_end_offset;

  // hang if host memory not defined
  if (!check_host_mem(args)) {
    printf("FATAL: host memory size not enough\n");
    for (;;)
      ;
  }

  uint32_t start = cycles();

  spin_segment_manipulate(&my_state->seg, stream_start_offset, &last,
                          &my_state->params);

  uint32_t end = cycles();

  // send back ack, if the remote requests for it
  if (SYN(flags)) {
    send_ack(hdrs, task);
  }

  // counter 0: per-packet average time
  push_counter(&__host_data.counters[0], end - start);
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {datatypes_hh, datatypes_ph, datatypes_th};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];

#ifdef USE_SPIN_SPINLOCK
  spin_lock_init(&spinlock);
#else
  amo_store(&lock_owner, 0);
#endif
}
