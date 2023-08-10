#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "fpspin/fpspin.h"

#include "../mpitypes/install/include/mpitypes_dataloop.h"

#include "../handlers/include/datatype_descr.h"
#include "../handlers/include/datatypes.h"
#include "../typebuilder/ddt_io_read.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
  uint32_t num_parallel_msgs;
  uint32_t elem_count;
} __attribute__((packed)) datatypes_rts_t;

static inline void *
prepare_ddt_nicmem(const char *ddt_file, struct mem_area handler_mem,
                   void **nic_buffer, size_t *nic_buffer_size,
                   uint32_t *num_elements, uint32_t *userbuf_size, uint32_t *streambuf_size) {
  // read ddt bin
  FILE *f = fopen(ddt_file, "rb");
  if (!f) {
    perror("open type descr");
    return NULL;
  }
  size_t datatype_mem_size = get_spin_datatype_size(f);
  void *datatype_mem_ptr_raw = malloc(datatype_mem_size);
  if (!datatype_mem_ptr_raw) {
    fprintf(stderr, "failed to allocate datatype buffer\n");
    return NULL;
  }

  if (handler_mem.size < datatype_mem_size) {
    fprintf(stderr, "handler mem too small to fit datatype: %d vs %ld\n",
            handler_mem.size, datatype_mem_size);
    return datatype_mem_ptr_raw;
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

  *num_elements = dtcount;
  *userbuf_size =
      dtinfo.true_lb + MAX(dtinfo.extent, dtinfo.true_extent) * dtcount;
  *streambuf_size = dtinfo.size * dtcount;
  // buffer area before copying onto the NIC
  *nic_buffer_size = sizeof(spin_datatype_mem_t) + datatype_mem_size +
                     sizeof(spin_core_state_t) * NUM_HPUS;
  *nic_buffer = malloc(*nic_buffer_size);
  if (!nic_buffer) {
    fprintf(stderr, "failed to allocate nic buffer\n");
    return datatype_mem_ptr_raw;
  }

  // layout: spin_datatype_mem_t | ddt | spin_core_state_t
  size_t nic_ddt_off = sizeof(spin_datatype_mem_t);

  fpspin_addr_t nic_ddt_pos = handler_mem.addr + nic_ddt_off;
  uint8_t *nic_buffer_ddt_data = (uint8_t *)*nic_buffer + nic_ddt_off;

  // relocate datatype for NIC
  memcpy(nic_buffer_ddt_data, datatype_mem_ptr_raw, datatype_mem_size);
  remap_spin_datatype(nic_buffer_ddt_data, datatype_mem_size, nic_ddt_pos,
                      true);

  spin_datatype_mem_t *nic_buffer_ddt_descr =
      (spin_datatype_mem_t *)*nic_buffer;
  spin_datatype_t *nic_buffer_dt = (spin_datatype_t *)nic_buffer_ddt_data;

  spin_core_state_t *nic_buffer_state =
      (spin_core_state_t *)(nic_buffer_ddt_data + datatype_mem_size);
  nic_buffer_ddt_descr->state =
      (spin_core_state_t *)(nic_ddt_pos + datatype_mem_size);

  for (int i = 0; i < NUM_HPUS; ++i) {
    // segment replicated onto each core
    nic_buffer_state[i].state = nic_buffer_dt->seg;
    nic_buffer_state[i].params = nic_buffer_dt->params;
  }

  return datatype_mem_ptr_raw;
}

#define RTS_PORT 9331