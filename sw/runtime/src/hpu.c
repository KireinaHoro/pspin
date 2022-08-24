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
#include "util.h"
#include "spin_conf.h"
#include "hwsched.h"
#include "pspin_rt.h"

#define MSTATUS_USER (3<<11)

#define read_register(reg, val) asm volatile ("addi %0, " STRINGIFY(reg) ", 0" : "=r"(val));
#define write_register(reg, val) asm volatile ("addi " STRINGIFY(reg) ", %0, 0" : : "r"(val) : "memory");

void __attribute__ ((aligned (256))) mtvec();

typedef struct hpu_descr {
    uint8_t *runtime_sp;
} hpu_descr_t;

volatile __attribute__ ((section(".data_tiny_l1"))) hpu_descr_t* volatile hpu_descr[NUM_CLUSTER_HPUS];

void hpu_run()
{
    handler_args_t handler_args;

    read_register(x10, handler_args.cluster_id);
    read_register(x11, handler_args.hpu_id);

    handler_args.hpu_gid = handler_args.cluster_id * NB_CORES + handler_args.hpu_id;
    handler_args.task = (task_t *) HWSCHED_HANDLER_MEM_ADDR;

    while (1) {

        handler_fn handler_fun = (handler_fn) MMIO_READ(HWSCHED_HANDLER_FUN_ADDR); 

        asm volatile ("nop"); /* TELEMETRY: HANDLER:START */
        handler_fun(&handler_args);
        asm volatile ("nop"); /* TELEMETRY: HANDLER:END */
       
        MMIO_READ(HWSCHED_DOORBELL); 
    }
}

void __attribute__ ((optimize(0))) cluster_init()
{
    uint32_t cluster_id = rt_cluster_id();
    enable_all_icache_banks();
    flush_all_icache_banks();
    hal_icache_cluster_enable(cluster_id);
}

void hpu_entry()
{

    uint32_t core_id = rt_core_id();
    uint32_t cluster_id = rt_cluster_id();

    printf("HPU (%lu, %lu) hello\n", cluster_id, core_id);

    if (core_id == 0 && cluster_id==0) {
        handler_fn hh, ph, th;
        void *handler_mem;
        init_handlers(&hh, &ph, &th, &handler_mem);
    }

    clear_csr(PULP_CSR_MSTATUS, MSTATUS_USER);
    write_csr(PULP_CSR_MEPC, hpu_run);

    write_csr(PULP_CSR_MTVEC, mtvec);

    //we save these now because can't access them in user mode
    write_register(x10, cluster_id);
    write_register(x11, core_id);

    //save the original sp in the HPU descr
    read_register(x2, hpu_descr[core_id]);

    //trap to user mode
    asm volatile("mret");   
}

void int0_handler()
{
    uint32_t mcause;
    read_csr(PULP_CSR_MCAUSE, mcause);
    /*
    switch (mcause)
    {
    case 8: // ECALL 
        handler_error("ecalls are not suppoerted");
        break;
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
    */

    MMIO_WRITE(HWSCHED_ERROR, mcause);
    MMIO_READ(HWSCHED_DOORBELL);

    //restore the stack pointer
    write_register(x2, hpu_descr[rt_core_id()]);

    //we want to resume the runtime and get ready
    //for the next handler
    clear_csr(PULP_CSR_MSTATUS, MSTATUS_USER);
    write_csr(PULP_CSR_MEPC, hpu_run);

    //trap to user mode
    asm volatile("mret");
}

void __attribute__ ((aligned (256))) mtvec()
{
    /* ecall */
    asm volatile("jalr x0, %0, 0" : : "r"(int0_handler));
}
