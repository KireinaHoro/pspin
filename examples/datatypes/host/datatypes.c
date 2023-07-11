#include "fpspin/fpspin.h"

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <immintrin.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../mpitypes/install/include/mpitypes_dataloop.h"
#include "../typebuilder/ddt_io_read.h"

#include "../handlers/datatype_descr.h"
#include "../handlers/datatypes.h"

#define PSPIN_DEV "/dev/pspin0"

volatile sig_atomic_t exit_flag = 0;
static void sigint_handler(int signum) { exit_flag = 1; }

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <ctx id> <type descr>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  struct sigaction sa = {
      .sa_handler = sigint_handler,
      .sa_flags = 0,
  };
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL)) {
    perror("sigaction");
    exit(EXIT_FAILURE);
  }

  FILE *f = fopen(argv[2], "rb");
  if (!f) {
    perror("open type descr");
    exit(EXIT_FAILURE);
  }
  size_t datatype_mem_size = get_spin_datatype_size(f);
  void *datatype_mem_ptr_raw = malloc(datatype_mem_size);
  if (!datatype_mem_ptr_raw) {
    fprintf(stderr, "failed to allocate datatype buffer\n");
    exit(EXIT_FAILURE);
  }

  read_spin_datatype(datatype_mem_ptr_raw, datatype_mem_size, f);
  fclose(f);

  spin_datatype_t *dt_header = (spin_datatype_t *)datatype_mem_ptr_raw;

  type_info_t dtinfo = dt_header->info;
  uint32_t dtcount = dt_header->count;
  uint32_t dtblocks = dt_header->blocks;

  printf("Count: %u\n", dtcount);
  printf("Blocks: %u\n", dtblocks);
  printf("Size: %li\n", dtinfo.size);
  printf("Extent: %li\n", dtinfo.extent);
  printf("True extent: %li\n", dtinfo.true_extent);
  printf("True LB: %li\n", dtinfo.true_lb);

  int dest_ctx = atoi(argv[1]);
  fpspin_ctx_t ctx;
  fpspin_ruleset_t rs;
  // custom ruleset
  fpspin_ruleset_slmp(&rs);
  if (!fpspin_init(&ctx, PSPIN_DEV, __IMG__, dest_ctx, &rs, 1)) {
    fprintf(stderr, "failed to initialise fpspin\n");
    goto fail;
  }

  if (ctx.handler_mem.size < datatype_mem_size) {
    fprintf(stderr, "handler mem too small to fit datatype: %d vs %ld\n",
            ctx.handler_mem.size, datatype_mem_size);
    goto fail;
  }

  // buffer area before copying onto the NIC
  size_t nic_buffer_size = sizeof(spin_datatype_mem_t) + datatype_mem_size +
                           sizeof(spin_core_state_t) * NUM_HPUS;
  void *nic_buffer = malloc(nic_buffer_size);
  if (!nic_buffer) {
    fprintf(stderr, "failed to allocate nic buffer\n");
    exit(EXIT_FAILURE);
  }

  fpspin_addr_t nic_mem_base = ctx.handler_mem.addr;
  // layout: spin_datatype_mem_t | ddt | spin_core_state_t
  size_t nic_ddt_off = sizeof(spin_datatype_mem_t);

  fpspin_addr_t nic_ddt_pos = nic_mem_base + nic_ddt_off;
  uint8_t *nic_buffer_ddt_data = (uint8_t *)nic_buffer + nic_ddt_off;

  // relocate datatype for NIC
  memcpy(nic_buffer_ddt_data, datatype_mem_ptr_raw, datatype_mem_size);
  remap_spin_datatype(nic_buffer_ddt_data, datatype_mem_size, nic_ddt_pos, true);

  spin_datatype_mem_t *nic_buffer_ddt_descr = (spin_datatype_mem_t *)nic_buffer;
  spin_datatype_t *nic_buffer_dt = (spin_datatype_t *)nic_buffer_ddt_data;

  spin_core_state_t *nic_buffer_state =
      (spin_core_state_t *)(nic_buffer_ddt_data + datatype_mem_size);
  nic_buffer_ddt_descr->state =
      (spin_core_state_t *)(nic_ddt_pos + datatype_mem_size);

  for (int i = 0; i < NUM_HPUS; ++i) {
    nic_buffer_state[i].state = nic_buffer_dt->seg;
    nic_buffer_state[i].params = nic_buffer_dt->params;
  }

  // copy to NIC memory
  fpspin_write_memory(&ctx, nic_mem_base, nic_buffer, nic_buffer_size);
  free(nic_buffer);
  free(datatype_mem_ptr_raw);

  // TODO: fastpath (_fp)

  // loading finished - application logic from here
  while (true) {
    if (exit_flag) {
      printf("\nReceived SIGINT, exiting...\n");
      break;
    }
    for (int i = 0; i < NUM_HPUS; ++i) {
      uint64_t flag_to_host;
      volatile uint8_t *pkt_addr;

      if (!(pkt_addr = fpspin_pop_req(&ctx, i, &flag_to_host)))
        continue;

      // got finished datatype from PsPIN
      // TODO: this should be asynchronous (to maximise overlapping ratio)
      uint64_t flag_from_host = MKFLAG(0);
      fpspin_push_resp(&ctx, i, flag_from_host);
    }
  }

  // get telemetry
  uint32_t avg_cycles = fpspin_get_avg_cycles(&ctx);
  printf("Handler cycles average: %d\n", avg_cycles);

  fpspin_exit(&ctx);
  return EXIT_SUCCESS;

fail:
  fpspin_exit(&ctx);
  return EXIT_FAILURE;
}
