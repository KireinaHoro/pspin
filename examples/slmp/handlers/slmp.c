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

#define DEBUG(...)                                                             \
  do {                                                                         \
    /* if (!args->hpu_gid) */                                                  \
    printf(__VA_ARGS__);                                                       \
  } while (0)

#define SIZE_MSG (128 * 1024 * 1024) // 128 MB
#define MSG_PAGES (SIZE_MSG / PAGE_SIZE)

static volatile uint32_t end_of_message;
static volatile uint32_t total_bytes;

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
  amo_store(&total_bytes, 0);

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

  DEBUG("EOM: %d; total bytes: %d\n", end_of_message, total_bytes);

  if (!SYN(flags) || !EOM(flags)) {
    printf("Error: last packet did not require SYN; flags = %#x\n", flags);
    return;
  }

  if (total_bytes != end_of_message) {
    printf("Error: total_bytes != end_of_message; %d vs %d\n", total_bytes,
           end_of_message);
    return;
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
  amo_add(&total_bytes, SLMP_PAYLOAD_LEN(hdrs));

  uint32_t current_tail = pkt_off + SLMP_PAYLOAD_LEN(hdrs);
  for (volatile uint32_t val = end_of_message;
       val < current_tail &&
       !compare_and_swap(&end_of_message, val, current_tail);)
    ;

  printf("total_bytes=%d end_of_message=%d\n", total_bytes, end_of_message);

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
}
