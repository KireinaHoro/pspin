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

#if 0
#define DEBUG(...)                                                             \
  do {                                                                         \
    /* if (!args->hpu_gid) */                                                  \
    printf(__VA_ARGS__);                                                       \
    rt_time_wait_cycles(10000);                                                \
  } while (0)
#endif
#define DEBUG(...)

#define SIZE_MSG (128 * 1024 * 1024) // 128 MB
#define MSG_PAGES (SIZE_MSG / PAGE_SIZE)

static volatile uint32_t end_of_message;
static volatile uint32_t end_of_message_amo;
static volatile uint32_t total_bytes;
static volatile uint32_t total_bytes_amo;
static volatile uint32_t lock_owner;

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

static inline bool check_host_mem(handler_args_t *args) {
  // extra MSG_PAGES pages after the messaging pages
  // DEBUG("Host memory size: %d\n", args->task->host_mem_size);
  return HOST_ADDR(args) &&
         args->task->host_mem_size >= PAGE_SIZE * (MSG_PAGES + CORE_COUNT);
}

#define SWAP(a, b, type)                                                       \
  do {                                                                         \
    type tmp = a;                                                              \
    a = b;                                                                     \
    b = tmp;                                                                   \
  } while (0)

void prepare_hdrs_ack(slmp_pkt_hdr_t *hdrs, uint16_t flags) {
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

__handler__ void slmp_hh(handler_args_t *args) {
  task_t *task = args->task;

  amo_store(&end_of_message, 0);
  amo_store(&end_of_message_amo, 0);
  amo_store(&total_bytes, 0);
  amo_store(&total_bytes_amo, 0);

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);
  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  DEBUG("New message: flow id %d, offset %d, payload size %d\n", task->flow_id,
        pkt_off, SLMP_PAYLOAD_LEN(hdrs));

  if (pkt_off != 0) {
    printf("Error: hh packet invoked with offset != 0: %d\n", pkt_off);
    return;
  }

  if (!SYN(flags)) {
    printf("Error: first packet did not require SYN; flags = %#x\n", flags);
    return;
  }

  // handshake to ensure only one message is in transmission (no interleaving)
  prepare_hdrs_ack(hdrs, MKACK);

  spin_cmd_t put;
  spin_send_packet(task->pkt_mem, sizeof(slmp_pkt_hdr_t), &put);

  DEBUG("Sent ACK for first packet\n");
}

__handler__ void slmp_th(handler_args_t *args) {
  task_t *task = args->task;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);
  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  DEBUG("End of message: flow id %d, offset %d, payload size %d\n",
        task->flow_id, pkt_off, SLMP_PAYLOAD_LEN(hdrs));

  DEBUG("EOM: %d (amo %d); total bytes: %d (amo %d)\n", end_of_message,
        end_of_message_amo, total_bytes, total_bytes_amo);

  if (!SYN(flags) || !EOM(flags)) {
    printf("Error: last packet did not require SYN; flags = %#x\n", flags);
    return;
  }

  if (total_bytes != end_of_message) {
    printf("Error: total_bytes != end_of_message; %d vs %d\n", total_bytes,
           end_of_message);
    // XXX: should we abort here? AMO is playing weird...
  }

  // notify host for unpacked message -- 0-byte host request
  // FIXME: this is synchronous; do we need an asynchronous interface?
  fpspin_host_req(args, end_of_message);

  // handshake to ensure only one message is in transmission (no interleaving)
  prepare_hdrs_ack(hdrs, MKACK);

  spin_cmd_t put;
  spin_send_packet(task->pkt_mem, sizeof(slmp_pkt_hdr_t), &put);

  DEBUG("Sent ACK for last packet\n");
}

__handler__ void slmp_ph(handler_args_t *args) {
  task_t *task = args->task;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint8_t *payload = (uint8_t *)task->pkt_mem + sizeof(slmp_pkt_hdr_t);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);

  DEBUG("Payload: flow id %d, offset %d, payload size %d\n", task->flow_id,
        pkt_off, SLMP_PAYLOAD_LEN(hdrs));

  // update tail and total bytes counter
  amo_add(&total_bytes_amo, SLMP_PAYLOAD_LEN(hdrs));
  uint32_t val;
  uint32_t new_sum;
  do {
    val = total_bytes;
    new_sum = val + SLMP_PAYLOAD_LEN(hdrs);
  } while (!compare_and_swap(&total_bytes, val, new_sum));

  uint32_t curr_tail = pkt_off + SLMP_PAYLOAD_LEN(hdrs);
  amo_maxu(&end_of_message_amo, curr_tail);
  do {
    val = end_of_message;
    if (curr_tail <= val)
      break;
  } while (!compare_and_swap(&end_of_message, val, curr_tail));

  DEBUG("EOM: %d (amo %d); total bytes: %d (amo %d)\n", end_of_message,
        end_of_message_amo, total_bytes, total_bytes_amo);

  if (!check_host_mem(args)) {
    printf("Host mem too small, skipping DMA\n");
    return;
  }

  // FIXME: DMA align?
  uint64_t host_start_addr = HOST_ADDR(args) + CORE_COUNT * PAGE_SIZE;
  spin_cmd_t cmd;
  spin_dma_to_host(host_start_addr + pkt_off, (uint32_t)payload,
                   SLMP_PAYLOAD_LEN(hdrs), 0, &cmd);
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {slmp_hh, slmp_ph, slmp_th};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];

  total_bytes = 0;
  total_bytes_amo = 0;
  end_of_message = 0;
  end_of_message_amo = 0;
}
