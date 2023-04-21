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

#include <handler.h>
#include <packets.h>
#include <spin_dma.h>

#if !defined(FROM_L2) && !defined(FROM_L1)
#define FROM_L1
#endif

__handler__ void pingpong_ph(handler_args_t *args) {
  printf("Packet @ %p (L2: %p) (size %d)\n", args->task->pkt_mem,
         args->task->l2_pkt_mem, args->task->pkt_mem_size);

  task_t *task = args->task;

  eth_hdr_t *eth_hdr = (eth_hdr_t *)(task->pkt_mem);

  ip_hdr_t *ip_hdr = (ip_hdr_t *)((uint8_t *)eth_hdr + sizeof(eth_hdr_t));
#ifdef FROM_L2
  uint8_t *nic_pld_addr = ((uint8_t *)(task->l2_pkt_mem));
#else
  uint8_t *nic_pld_addr = ((uint8_t *)(task->pkt_mem));
#endif

  udp_hdr_t *udp_hdr = (udp_hdr_t *)((uint8_t *)ip_hdr + ip_hdr->ihl * 4);

  uint16_t pkt_pld_len = args->task->pkt_mem_size;

  mac_addr_t src_mac = eth_hdr->src;
  eth_hdr->src = eth_hdr->dest;
  eth_hdr->dest = src_mac;

  uint32_t src_id = ip_hdr->source_id;
  ip_hdr->source_id = ip_hdr->dest_id;
  ip_hdr->dest_id = src_id;

  uint16_t src_port = udp_hdr->src_port;
  udp_hdr->src_port = udp_hdr->dst_port;
  udp_hdr->dst_port = src_port;

  spin_cmd_t put;
  spin_send_packet(nic_pld_addr, pkt_pld_len, &put);

  // It's not strictly necessary to wait. The hw will enforce that the feedback
  // is not sent until all commands issued by this handlers are completed.
#ifdef WAIT_POLL
  bool completed = false;
  do {
    spin_cmd_test(put, &completed);
  } while (!completed);
#elif defined(WAIT_SUSPEND)
  spin_cmd_wait(put);
#endif
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {NULL, pingpong_ph, NULL};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];
}
