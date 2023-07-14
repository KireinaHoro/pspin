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

#pragma once

#include <assert.h>
#include <hal/pulp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define PAGE_SIZE 4096

#if RISCV_VERSION >= 4 && !defined(RISCV_1_7)
#if PULP_CHIP_FAMILY == CHIP_GAP
#define PULP_CSR_MHARTID 0x014
#else
#define PULP_CSR_MHARTID 0xf14
#endif
#else
#define PULP_CSR_MHARTID 0xf10
#endif

#define PULP_CSR_MSTATUS 0x300
#define PULP_CSR_MTVEC   0x305
#define PULP_CSR_MCOUNTINHIBIT 0x320
#define PULP_CSR_MEPC    0x341
#define PULP_CSR_MCAUSE  0x342
#define PULP_CSR_MTVAL   0x343
#define PULP_CSR_PRIVLV  0xc10

#define PULP_CSR_PMPCFG0 0x3a0
#define PULP_CSR_PMPCFG1 0x3a1
#define PULP_CSR_PMPCFG2 0x3a2
#define PULP_CSR_PMPCFG3 0x3a3
#define PULP_CSR_PMPADDR0 0x3b0
#define PULP_CSR_PMPADDR1 0x3b1
#define PULP_CSR_PMPADDR2 0x3b2
#define PULP_CSR_PMPADDR3 0x3b3
#define PULP_CSR_PMPADDR4 0x3b4
#define PULP_CSR_PMPADDR5 0x3b5
#define PULP_CSR_PMPADDR6 0x3b6
#define PULP_CSR_PMPADDR7 0x3b7
#define PULP_CSR_PMPADDR8 0x3b8
#define PULP_CSR_PMPADDR9 0x3b9
#define PULP_CSR_PMPADDR10 0x3ba
#define PULP_CSR_PMPADDR11 0x3bb
#define PULP_CSR_PMPADDR12 0x3bc
#define PULP_CSR_PMPADDR13 0x3bd
#define PULP_CSR_PMPADDR14 0x3be
#define PULP_CSR_PMPADDR15 0x3bf

// ecalls
#define PSPIN_ECALL_CYCLES 0x1
#define ecall(num, a0, a1, a2, a3, a4, a5, a6, ret)                            \
  asm volatile("mv a0,%1; mv a1,%2; mv a2,%3; mv a3,%4; mv a4,%5; mv a5,%6; "  \
               "mv a6,%7; mv a7,%8; ecall; mv %0, a0"                          \
               : "=r"(ret)                                                     \
               : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5),         \
                 "r"(a6), "r"(num)                                             \
               : "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7");
#define ecall_1(num, a0, ret) ecall(num, a0, 0, 0, 0, 0, 0, 0, ret)
#define ecall_0(num, ret) ecall_1(num, 0, ret)

#ifndef NO_PULP
static inline uint32_t rt_core_id() { return hal_core_id(); }

static inline uint32_t rt_cluster_id() { return hal_cluster_id(); }
#endif
