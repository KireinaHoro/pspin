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

#include "hpu.h"
#include "hwsched.h"
#include "pspin_rt.h"
#include "spin_conf.h"
#include "util.h"

#define MSTATUS_USER (3 << 11)

extern void rt_vec();

typedef struct hpu_descr {
  uint8_t *runtime_sp;
} hpu_descr_t;

volatile __attribute__((section(".data_tiny_l1")))
hpu_descr_t *volatile hpu_descr[NUM_CLUSTER_HPUS];

// virtualised with each cluster
volatile __attribute__((section(".data_tiny_l1")))
uint8_t dma_idx[NUM_CLUSTER_HPUS];

void hpu_run() {
  handler_args_t handler_args;

  read_register(x10, handler_args.cluster_id);
  read_register(x11, handler_args.hpu_id);

  handler_args.hpu_gid =
      handler_args.cluster_id * NB_CORES + handler_args.hpu_id;
  handler_args.task = (task_t *)HWSCHED_HANDLER_MEM_ADDR;

  while (1) {

    handler_fn handler_fun = (handler_fn)MMIO_READ(HWSCHED_HANDLER_FUN_ADDR);

    asm volatile("nop"); /* TELEMETRY: HANDLER:START */
    handler_fun(&handler_args);
    asm volatile("nop"); /* TELEMETRY: HANDLER:END */

    MMIO_READ(HWSCHED_DOORBELL);
  }
}

void __attribute__((optimize(0))) cluster_init() {
  uint32_t cluster_id = rt_cluster_id();
  enable_all_icache_banks();
  flush_all_icache_banks();
  hal_icache_cluster_enable(cluster_id);
}

// FIXME: can we do this entirely in the linker script?
extern uintptr_t __l2_heap_start, __l2_heap_size;
void *UMM_MALLOC_CFG_HEAP_ADDR;
uint32_t UMM_MALLOC_CFG_HEAP_SIZE;

void hpu_entry() {

  uint32_t core_id = rt_core_id();
  uint32_t cluster_id = rt_cluster_id();

  // save mhartid before first printf
  uint32_t mhartid;
  read_csr(PULP_CSR_MHARTID, mhartid);
  write_register(tp, mhartid);

  // if (cluster_id == 0 && core_id == 1)
  printf("HPU (%lu, %lu) hello from %s\n", cluster_id, core_id, __func__);

  // clear & enable counters
  uint32_t reset_val = 0;
  write_csr(mcycle, reset_val);
  write_csr(minstret, reset_val);

  uint32_t counter_mask;
  read_csr(PULP_CSR_MCOUNTINHIBIT, counter_mask);
  counter_mask &= ~0b101; // MCYCLE & MINSTRET
  write_csr(PULP_CSR_MCOUNTINHIBIT, counter_mask);

  clear_csr(PULP_CSR_MSTATUS, MSTATUS_USER);
  write_csr(PULP_CSR_MEPC, hpu_run);

  write_csr(PULP_CSR_MTVEC, rt_vec);

  // after exception handler so we can catch errors in user init
  if (core_id == 0 && cluster_id == 0) {
    handler_fn hh, ph, th;
    void *handler_mem;
    init_handlers(&hh, &ph, &th, &handler_mem);

    // initialise performance counters
    for (int i = 0; i < MAX_COUNTERS; ++i) {
      __host_data.counters[i].count = 0;
      __host_data.counters[i].sum = 0;
    }
  }

  /*
  UMM_MALLOC_CFG_HEAP_ADDR = &__l2_heap_start;
  UMM_MALLOC_CFG_HEAP_SIZE = (uint32_t)&__l2_heap_size;

  if (core_id == 0) {
    umm_init();
    if (cluster_id == 0) {
      umm_info(0, true);
    }
  }
  */

  // we save these now because can't access them in user mode
  write_register(x10, cluster_id);
  write_register(x11, core_id);

  // save the original sp in the HPU descr
  read_register(x2, hpu_descr[core_id]);

  // trap to user mode
  asm volatile("mret");
}

#define handler_error(msg) printf("TRAP @ %#x: " msg "\n", mepc)

typedef struct {
  uint32_t ra;
  // not saving: sp, gp, tp
  uint32_t a0, a1, a2, a3, a4, a5, a6, a7;
  uint32_t t0, t1, t2, t3, t4, t5, t6;
  uint32_t s0, s1, s2, s3, s4, s5, s6, s7, s8, s9, s10, s11;
} __attribute__((packed)) saved_regs_t;

void dump_regs(volatile saved_regs_t *a) {
  printf("ra=0x%08x\n", a->ra);
  printf("a0=0x%08x\n", a->a0);
  printf("a1=0x%08x\n", a->a1);
  printf("a2=0x%08x\n", a->a2);
  printf("a3=0x%08x\n", a->a3);
  printf("a4=0x%08x\n", a->a4);
  printf("a5=0x%08x\n", a->a5);
  printf("a6=0x%08x\n", a->a6);
  printf("a7=0x%08x\n", a->a7);
  printf("t0=0x%08x\n", a->t0);
  printf("t1=0x%08x\n", a->t1);
  printf("t2=0x%08x\n", a->t2);
  printf("t3=0x%08x\n", a->t3);
  printf("t4=0x%08x\n", a->t4);
  printf("t5=0x%08x\n", a->t5);
  printf("t6=0x%08x\n", a->t6);
  printf("s0=0x%08x\n", a->s0);
  printf("s1=0x%08x\n", a->s1);
  printf("s2=0x%08x\n", a->s2);
  printf("s3=0x%08x\n", a->s3);
  printf("s4=0x%08x\n", a->s4);
  printf("s5=0x%08x\n", a->s5);
  printf("s6=0x%08x\n", a->s6);
  printf("s7=0x%08x\n", a->s7);
  printf("s8=0x%08x\n", a->s8);
  printf("s9=0x%08x\n", a->s9);
  printf("s10=0x%08x\n", a->s10);
  printf("s11=0x%08x\n", a->s11);
}

void int0_handler() {
  uint32_t mcause, mepc;
  read_csr(PULP_CSR_MCAUSE, mcause);
  read_csr(PULP_CSR_MEPC, mepc);
  // mtval not implemented in PULP

  // force earlier check of syscall (fastpath)
  if (__builtin_expect(mcause, 8) == 8) {
    // get saved area
    uint32_t saved;
    read_csr(mscratch, saved);
    volatile saved_regs_t *saved_regs = (saved_regs_t *)saved;
    bool handled = false;

    // printf("Ecall: %d @ %#x\n", saved_regs->a7, mepc);
    // dump_regs(saved_regs);
    switch (saved_regs->a7) {
    case PSPIN_ECALL_CYCLES:
      read_csr(mcycle, saved_regs->a0);
      handled = true;
      break;
    default:
      handler_error("Unknown ecall number");
    }
    if (handled) {
      uint32_t resume = mepc + 4;
      // printf("Returning to %#x\n", resume);
      // dump_regs(saved_regs);
      clear_csr(PULP_CSR_MSTATUS, MSTATUS_USER);
      write_csr(PULP_CSR_MEPC, resume);
      return;
    }
  } else
    switch (mcause) {
    case 8: { // ECALL
    }
    case 1:
      handler_error("Instruction access fault");
      break;
    case 2:
      handler_error("Illegal instruction");
      break;
    case 5:
      handler_error("Load access fault");
      break;
    case 7:
      handler_error("Store/AMO access fault");
      break;
    default:
      handler_error("Unrecognized mcause");
      break;
    }

  // diagnostics
  uint32_t saved;
  read_csr(mscratch, saved);
  volatile saved_regs_t *saved_regs = (saved_regs_t *)saved;
  dump_regs(saved_regs);
  for (;;)
    ;

  MMIO_WRITE(HWSCHED_ERROR, mcause);
  MMIO_READ(HWSCHED_DOORBELL);

  // restore the stack pointer
  write_register(x2, hpu_descr[rt_core_id()]);

  // we want to resume the runtime and get ready
  // for the next handler
  clear_csr(PULP_CSR_MSTATUS, MSTATUS_USER);
  write_csr(PULP_CSR_MEPC, hpu_run);

  // trap to user mode -- not restoring context
  asm volatile("mret");
}