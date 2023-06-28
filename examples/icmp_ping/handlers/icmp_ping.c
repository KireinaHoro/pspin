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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(FROM_L2) && !defined(FROM_L1)
#define FROM_L1
#endif

#define DO_HOST true

#define DEBUG(...) printf(__VA_ARGS__)
// #define DEBUG(...)

#define PAGE_SIZE 4096
#define HPU_ID(args) (args->cluster_id * NB_CORES + args->hpu_id)
#define HOST_ADDR(args)                                                        \
  (((uint64_t)args->task->host_mem_high << 32) | args->task->host_mem_low)
#define HOST_ADDR_HPU(args) (HOST_ADDR(args) + HPU_ID(args) * PAGE_SIZE)

#define FLAG_DMA_ID(fl) ((fl)&0xf)
#define FLAG_LEN(fl) (((fl) >> 8) & 0xff)
#define FLAG_HPU_ID(fl) ((fl) >> 24 & 0xff)
#define MKFLAG(id, len, hpuid)                                                 \
  (((id)&0xf) | (((len)&0xff) << 8) | (((hpuid)&0xff) << 24))
#define DMA_BUS_WIDTH 512
#define DMA_ALIGN (DMA_BUS_WIDTH / 8)

// TODO: refactor into common facility
static uint8_t dma_idx[CORE_COUNT];

static volatile int32_t inflight_messages = 0;

static inline uint16_t bswap_16(uint16_t v) {
  return ((v & 0xff) << 8) | (v >> 8);
}

#define htons(x) bswap_16(x)
#define ntohs htons

// FIXME: only for ICMP echo
typedef struct {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint32_t rest_of_header;
} icmp_hdr_t;

typedef struct {
  eth_hdr_t eth_hdr;
  ip_hdr_t ip_hdr; // FIXME: assumes ihl=4
  icmp_hdr_t icmp_hdr;
} __attribute__((__packed__)) hdr_t;

uint16_t ip_checksum(void *vdata, size_t length) {
  // Cast the data pointer to one that can be indexed.
  char *data = (char *)vdata;

  // Initialise the accumulator.
  uint32_t acc = 0xffff;

  // Handle complete 16-bit blocks.
  for (size_t i = 0; i + 1 < length; i += 2) {
    uint16_t word;
    memcpy(&word, data + i, 2);
    acc += ntohs(word);
    if (acc > 0xffff) {
      acc -= 0xffff;
    }
  }

  // Handle any partial block at the end of the data.
  if (length & 1) {
    uint16_t word = 0;
    memcpy(&word, data + length - 1, 1);
    acc += ntohs(word);
    if (acc > 0xffff) {
      acc -= 0xffff;
    }
  }

  // Return the checksum in network byte order.
  return htons(~acc);
}

__handler__ void pingpong_hh(handler_args_t *args) {
  DEBUG(
      "Start of message: @ %p (L2: %p) (size %d) flow_id %d inflight msgs %d\n",
      args->task->pkt_mem, args->task->l2_pkt_mem, args->task->pkt_mem_size,
      args->task->flow_id, inflight_messages);
  amo_add(&inflight_messages, 1);
}

__handler__ void pingpong_th(handler_args_t *args) {
  DEBUG("End of message: @ %p (L2: %p) (size %d) flow_id %d inflight msgs %d\n",
        args->task->pkt_mem, args->task->l2_pkt_mem, args->task->pkt_mem_size,
        args->task->flow_id, inflight_messages);
  amo_add(&inflight_messages, -1);
}

__handler__ void pingpong_ph(handler_args_t *args) {
  DEBUG("Packet @ %p (L2: %p) (size %d) flow_id %d inflight msgs %d\n",
        args->task->pkt_mem, args->task->l2_pkt_mem, args->task->pkt_mem_size,
        args->task->flow_id, inflight_messages);

  task_t *task = args->task;

  hdr_t *hdrs = (hdr_t *)(task->pkt_mem);
  uint16_t pkt_len = args->task->pkt_mem_size;
#ifdef FROM_L2
  uint8_t *nic_pld_addr = ((uint8_t *)(task->l2_pkt_mem));
#else
  uint8_t *nic_pld_addr = ((uint8_t *)(task->pkt_mem));
#endif

  // ICMP ping: swap MAC and IP address
  mac_addr_t src_mac = hdrs->eth_hdr.src;
  hdrs->eth_hdr.src = hdrs->eth_hdr.dest;
  hdrs->eth_hdr.dest = src_mac;

  uint32_t src_id = hdrs->ip_hdr.source_id;
  hdrs->ip_hdr.source_id = hdrs->ip_hdr.dest_id;
  hdrs->ip_hdr.dest_id = src_id;

  spin_cmd_t dma;
  uint64_t flag_haddr = HOST_ADDR_HPU(args),
           pld_haddr = HOST_ADDR_HPU(args) + DMA_ALIGN;

  if (DO_HOST && HOST_ADDR(args) && args->task->host_mem_size >= CORE_COUNT * PAGE_SIZE) {
    DEBUG("Host flag addr: %#llx\n", flag_haddr);

    // DMA packet data
    spin_dma_to_host(pld_haddr, (uint32_t)nic_pld_addr, pkt_len, 1, &dma);
    spin_cmd_wait(dma);
    DEBUG("Written packet data\n");

    // FIXME: provide library function for comm to host
    // prepare host notification
    ++dma_idx[HPU_ID(args)];
    volatile uint64_t flag_to_host =
        MKFLAG(dma_idx[HPU_ID(args)], pkt_len, HPU_ID(args));

    // write flag
    spin_write_to_host(flag_haddr, flag_to_host, &dma);
    spin_cmd_wait(dma);

    // poll for host finish
    uint64_t flag_from_host;
    do {
      flag_from_host = __host_flag[HPU_ID(args)];
    } while (FLAG_DMA_ID(flag_to_host) != FLAG_DMA_ID(flag_from_host));
    // FIXME: end

    uint16_t host_pkt_len = FLAG_LEN(flag_from_host);

    // DMA packet data back
    spin_dma_from_host(pld_haddr, (uint32_t)nic_pld_addr, host_pkt_len, 1,
                       &dma);
    spin_cmd_wait(dma);

    DEBUG("DMA roundtrip finished, packet from host size: %d\n", host_pkt_len);
    if (FLAG_HPU_ID(flag_from_host) != HPU_ID(args)) {
      printf("HPU ID mismatch in response flag!  Got: %lld\n",
             FLAG_HPU_ID(flag_from_host));
    }
    pkt_len = host_pkt_len;
  } else {
    // calculate ICMP checksum ourselves
    uint16_t ip_len = ntohs(hdrs->ip_hdr.length);
    uint16_t icmp_len = ip_len - sizeof(ip_hdr_t);
    hdrs->icmp_hdr.type = 0; // Echo-Reply
    hdrs->icmp_hdr.checksum = 0;
    hdrs->icmp_hdr.checksum =
        ip_checksum((uint8_t *)&hdrs->icmp_hdr, icmp_len);
    pkt_len = ip_len + sizeof(eth_hdr_t);
  }

  spin_cmd_t put;
  spin_send_packet(nic_pld_addr, pkt_len, &put);
  spin_cmd_wait(put);
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {pingpong_hh, pingpong_ph, pingpong_th};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];
}
