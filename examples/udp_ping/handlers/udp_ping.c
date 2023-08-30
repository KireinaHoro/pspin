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

#if !defined(FROM_L2) && !defined(FROM_L1)
#define FROM_L1
#endif

#define DEBUG(...)
// #define DEBUG(...) printf(__VA_ARGS__)

__handler__ void pingpong_ph(handler_args_t *args) {
  // counter 2: estimate latency of cycles()
  uint32_t start_cycles = cycles();
  uint32_t start = cycles();
  push_counter(&__host_data.counters[2], start - start_cycles);

  DEBUG("Packet @ %p (L2: %p) (size %d) flow_id %d \n", args->task->pkt_mem,
        args->task->l2_pkt_mem, args->task->pkt_mem_size, args->task->flow_id);

  task_t *task = args->task;

  pkt_hdr_t *hdrs = (pkt_hdr_t *)(task->pkt_mem);
  uint16_t pkt_len = args->task->pkt_mem_size;
#ifdef FROM_L2
  uint8_t *nic_pld_addr = ((uint8_t *)(task->l2_pkt_mem));
#else
  uint8_t *nic_pld_addr = ((uint8_t *)(task->pkt_mem));
#endif

  mac_addr_t src_mac = hdrs->eth_hdr.src;
  hdrs->eth_hdr.src = hdrs->eth_hdr.dest;
  hdrs->eth_hdr.dest = src_mac;

  uint32_t src_id = hdrs->ip_hdr.source_id;
  hdrs->ip_hdr.source_id = hdrs->ip_hdr.dest_id;
  hdrs->ip_hdr.dest_id = src_id;

  uint16_t src_port = hdrs->udp_hdr.src_port;
  hdrs->udp_hdr.src_port = hdrs->udp_hdr.dst_port;
  hdrs->udp_hdr.dst_port = src_port;

  spin_cmd_t dma;
  fpspin_flag_t flag_from_host;

  if (DO_HOST && fpspin_check_host_mem(args)) {
    // DMA packet data
    uint32_t host_start = cycles();
    spin_dma_to_host(HOST_PLD_ADDR(args), (uint32_t)nic_pld_addr, pkt_len, 1,
                     &dma);
    spin_cmd_wait(dma);
    DEBUG("Written packet data\n");

    flag_from_host = fpspin_host_req(args, pkt_len);
    uint16_t host_pkt_len = flag_from_host.len;

    // DMA packet data back
    spin_dma_from_host(HOST_PLD_ADDR(args), (uint32_t)nic_pld_addr,
                       host_pkt_len, 1, &dma);
    spin_cmd_wait(dma);

    // counter 1: host mem access time
    push_counter(&__host_data.counters[1], cycles() - host_start);

    DEBUG("DMA roundtrip finished, packet from host size: %d\n", host_pkt_len);
    pkt_len = host_pkt_len;
  }

  spin_cmd_t put;
  spin_send_packet(nic_pld_addr, pkt_len, &put);
  spin_cmd_wait(put);

  // counter 0: handler time
  uint32_t end = cycles();
  push_counter(&__host_data.counters[0], end - start);
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {NULL, pingpong_ph, NULL};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];
}
