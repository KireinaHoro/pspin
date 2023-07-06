#pragma once

#define HPU_ID(args) (args->hpu_gid)
#define HOST_ADDR(args)                                                        \
  (((uint64_t)args->task->host_mem_high << 32) | args->task->host_mem_low)
#define HOST_ADDR_HPU(args) (HOST_ADDR(args) + HPU_ID(args) * PAGE_SIZE)
#define HOST_PLD_ADDR(args) (HOST_ADDR_HPU(args) + DMA_ALIGN)

#define FLAG_DMA_ID(fl) ((fl)&0xf)
#define FLAG_LEN(fl) (((fl) >> 8) & 0xffff)
#define FLAG_HPU_ID(fl) ((fl) >> 24 & 0xff)
#define MKFLAG(id, len, hpuid)                                                 \
  (((id)&0xf) | (((len)&0xffff) << 8) | (((hpuid)&0xff) << 24))
#define DMA_BUS_WIDTH 512
#define DMA_ALIGN (DMA_BUS_WIDTH / 8)

extern uint8_t dma_idx[CORE_COUNT];

static inline bool fpspin_check_host_mem(handler_args_t *args) {
  return HOST_ADDR(args) && args->task->host_mem_size >= CORE_COUNT * PAGE_SIZE;
}

static inline uint64_t fpspin_host_req(handler_args_t *args, uint16_t len) {
  uint64_t flag_haddr = HOST_ADDR_HPU(args);
  spin_cmd_t dma;

  // prepare host notification
  ++dma_idx[HPU_ID(args)];
  volatile uint64_t flag_to_host =
      MKFLAG(dma_idx[HPU_ID(args)], len, HPU_ID(args));

  // write flag
  spin_write_to_host(flag_haddr, flag_to_host, &dma);
  spin_cmd_wait(dma);

  // poll for host finish
  uint64_t flag_from_host;
  do {
    flag_from_host = __host_data.flag[HPU_ID(args)];
  } while (FLAG_DMA_ID(flag_to_host) != FLAG_DMA_ID(flag_from_host));

  if (FLAG_HPU_ID(flag_from_host) != HPU_ID(args)) {
    printf("HPU ID mismatch in response flag!  Got: %lld\n",
           FLAG_HPU_ID(flag_from_host));
  }
  return flag_from_host;
}
