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
  } while (0)
#else
#define DEBUG(...)
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

uint32_t start_file;

__handler__ void slmp_hh(handler_args_t *args) {
  // counter 0: estimate latency of cycles()
  // uint32_t start_cycles = cycles();
  // counter 1: latency for whole messages
  // start_file = cycles();
  // push_counter(&__host_data.counters[0], start_file - start_cycles);

  task_t *task = args->task;

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

  // counter 5: head/tail
  // push_counter(&__host_data.counters[5], cycles() - start_file);
}

__handler__ void slmp_th(handler_args_t *args) {
  task_t *task = args->task;

  // uint32_t start_tail = cycles();

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);
  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  DEBUG("End of message: flow id %d, offset %d, payload size %d\n",
        task->flow_id, pkt_off, SLMP_PAYLOAD_LEN(hdrs));

  if (!EOM(flags)) {
    printf("Error: last packet did not have EOM; flags = %#x\n", flags);
    return;
  }

  uint32_t total_len = pkt_off + SLMP_PAYLOAD_LEN(hdrs);

  // counter 4: host notification
  // uint32_t host_start = cycles();
  fpspin_host_req(args, total_len);
  // uint32_t host_end = cycles();
  DEBUG("host_start=%d host_end=%d\n", host_start, host_end);
  // push_counter(&__host_data.counters[4], host_end - host_start);

  // uint32_t end_tail = cycles();

  // counter 1: latency for whole messages
  // push_counter(&__host_data.counters[1], end_tail - start_file);

  // counter 5: head/tail
  // push_counter(&__host_data.counters[5], end_tail - start_tail);
}

__handler__ void slmp_ph(handler_args_t *args) {
  // counter 2: per-packet latency
  // uint32_t start = cycles();

  task_t *task = args->task;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint8_t *payload = (uint8_t *)task->pkt_mem + sizeof(slmp_pkt_hdr_t);
  uint32_t pkt_off = ntohl(hdrs->slmp_hdr.pkt_off);
  uint16_t flags = ntohs(hdrs->slmp_hdr.flags);

  DEBUG("Payload: flow id %d, offset %d, payload size %d\n", task->flow_id,
        pkt_off, SLMP_PAYLOAD_LEN(hdrs));

  // counter 3: host DMA
  // uint32_t host_start = cycles();
  uint64_t host_start_addr = HOST_ADDR(args) + CORE_COUNT * PAGE_SIZE;
  spin_cmd_t cmd;
  spin_dma_to_host(host_start_addr + pkt_off, (uint32_t)payload,
                   SLMP_PAYLOAD_LEN(hdrs), 0, &cmd);
  // spin_cmd_wait(cmd);
  // push_counter(&__host_data.counters[3], cycles() - host_start);

  // send back ack, if the remote requests for it
  if (SYN(flags)) {
    send_ack(hdrs, task);
  }

  // push_counter(&__host_data.counters[2], cycles() - start);
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {slmp_hh, slmp_ph, slmp_th};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];
}
