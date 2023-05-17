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

#if !defined(FROM_L2) && !defined(FROM_L1)
#define FROM_L1
#endif

#define NUM_HPUS_PER_CLUSTER 8
#define NUM_CLUSTERS 2
#define NUM_HPUS (NUM_HPUS_PER_CLUSTER * NUM_CLUSTERS)
#define PAGE_SIZE 4096
#define HPU_ID(args) (args->cluster_id * NUM_HPUS_PER_CLUSTER + args->hpu_id)
#define HOST_ADDR(args)                                                        \
  (((uint64_t)args->task->host_mem_high << 32) | args->task->host_mem_low)
#define HOST_ADDR_HPU(args) (HOST_ADDR(args) + HPU_ID(args) * PAGE_SIZE)

#define FLAG_DMA_ID(fl) ((fl)&0xf)
#define FLAG_LEN(fl) (((fl) >> 8) & 0xff)
#define FLAG_HPU_ID(fl) ((fl) >> 24 & 0xff)
#define MKFLAG(id, len, hpuid)                                                 \
  (((id)&0xf) | (((len)&0xff) << 8) | (((hpuid)&0xff) << 24))

extern volatile uint64_t __host_flag[NUM_HPUS];
static uint8_t dma_idx[NUM_HPUS];
static volatile uint32_t lock_owner;

// stock spinlock stuck at lock ??
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

__handler__ void pingpong_hh(handler_args_t *args) {
  if (!HPU_ID(args)) {
    printf("Ping pong head handler\n");
    amo_store(&lock_owner, 0);
  }
}

__handler__ void pingpong_ph(handler_args_t *args) {
  printf("Packet @ %p (L2: %p) (size %d) (lock owner: %d)\n",
         args->task->pkt_mem, args->task->l2_pkt_mem, args->task->pkt_mem_size,
         lock_owner);

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

  spin_cmd_t dma;
  uint64_t flag_haddr = HOST_ADDR_HPU(args),
           pld_haddr = HOST_ADDR_HPU(args) + sizeof(uint64_t);

  if (HOST_ADDR(args) && args->task->host_mem_size >= NUM_HPUS * PAGE_SIZE) {
    printf("Host flag addr: %#llx\n", flag_haddr);

    // DMA packet data
    spin_dma_to_host(pld_haddr, (uint32_t)nic_pld_addr, pkt_pld_len, 1, &dma);
    do {
      spin_cmd_test(dma, &completed);
    } while (!completed);
    printf("Written packet data\n");

    // prepare host notification
    ++dma_idx[HPU_ID(args)];
    uint64_t flag_to_host =
        MKFLAG(dma_idx[HPU_ID(args)], pkt_pld_len, HPU_ID(args));

    // write flag
    spin_write_to_host(flag_haddr, flag_to_host, &dma);
    do {
      spin_cmd_test(dma, &completed);
    } while (!completed);
    printf("Flag %#llx (id %#llx, len %lld) written to host\n", flag_to_host,
           FLAG_DMA_ID(flag_to_host), FLAG_LEN(flag_to_host));

    // poll for host finish
    uint64_t flag_from_host;
    do {
      flag_from_host = __host_flag[HPU_ID(args)];
    } while (FLAG_DMA_ID(flag_from_host) != dma_idx[HPU_ID(args)]);

    uint16_t host_pkt_len = FLAG_LEN(flag_from_host);

    // DMA packet data back
    spin_dma_from_host(pld_haddr, (uint32_t)nic_pld_addr, host_pkt_len, 1,
                       &dma);
    do {
      spin_cmd_test(dma, &completed);
    } while (!completed);

    printf("DMA roundtrip finished, packet from host size: %d\n", host_pkt_len);
    if (FLAG_HPU_ID(flag_from_host) != HPU_ID(args)) {
      printf("HPU ID mismatch in response flag!  Got: %lld\n",
             FLAG_HPU_ID(flag_from_host));
    }
  }

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
