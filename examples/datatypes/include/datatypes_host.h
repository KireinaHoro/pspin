#pragma once

#include <stdint.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

typedef struct {
  uint32_t num_parallel_msgs;
  uint32_t elem_count;
} __attribute__((packed)) datatypes_rts_t;

#define RTS_PORT 9331