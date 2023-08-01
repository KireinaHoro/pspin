#pragma once

#include "spin_conf.h"
#include <stdint.h>
typedef struct {
  union {
    struct {
      uint8_t dma_id;
      uint32_t len;
      uint16_t hpu_id;
    } __attribute__((packed));
    uint64_t data;
  };
} fpspin_flag_t;
static_assert(sizeof(fpspin_flag_t) == sizeof(uint64_t),
              "flag size not correct");

#define HPU_ID(args) (args->hpu_gid)
#define HOST_ADDR(args)                                                        \
  (((uint64_t)args->task->host_mem_high << 32) | args->task->host_mem_low)
#define HOST_ADDR_HPU(args) (HOST_ADDR(args) + HPU_ID(args) * PAGE_SIZE)
#define HOST_PLD_ADDR(args) (HOST_ADDR_HPU(args) + DMA_ALIGN)

#define DMA_BUS_WIDTH 512
#define DMA_ALIGN (DMA_BUS_WIDTH / 8)

extern volatile uint8_t dma_idx[NUM_CLUSTER_HPUS];

static inline bool fpspin_check_host_mem(handler_args_t *args) {
  return HOST_ADDR(args) && args->task->host_mem_size >= CORE_COUNT * PAGE_SIZE;
}

static inline fpspin_flag_t fpspin_host_req(handler_args_t *args, uint32_t len) {
  uint64_t flag_haddr = HOST_ADDR_HPU(args);
  spin_cmd_t dma;

  // prepare host notification
  fpspin_flag_t flag_to_host = {
      .dma_id = ++dma_idx[args->hpu_id],
      .len = len,
      .hpu_id = HPU_ID(args),
  };

  // write flag
  spin_write_to_host(flag_haddr, flag_to_host.data, &dma);
  spin_cmd_wait(dma);

  // poll for host finish
  fpspin_flag_t flag_from_host;
  do {
    flag_from_host.data = __host_data.flag[HPU_ID(args)];
  } while (flag_to_host.dma_id != flag_from_host.dma_id);

  if (flag_from_host.hpu_id != HPU_ID(args)) {
    printf("HPU ID mismatch in response flag!  Got: %lld\n",
           flag_from_host.hpu_id);
  }
  return flag_from_host;
}