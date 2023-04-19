
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

#ifndef HOST
#include <handler.h>
#include <packets.h>
#include <spin_dma.h>
#else
#include <handler_profiler.h>
#endif

#ifndef NUM_INT_OP
#define NUM_INT_OP 0
#endif

volatile __attribute__((section(".l2_handler_data")))
uint8_t handler_mem[] = {0xde, 0xad, 0xbe, 0xef};

void hexdump(const void *data, size_t size) {
  char ascii[17];
  size_t i, j;
  ascii[16] = '\0';
  for (i = 0; i < size; ++i) {
    printf("%02X ", ((unsigned char *)data)[i]);
    if (((unsigned char *)data)[i] >= ' ' &&
        ((unsigned char *)data)[i] <= '~') {
      ascii[i % 16] = ((unsigned char *)data)[i];
    } else {
      ascii[i % 16] = '.';
    }
    if ((i + 1) % 8 == 0 || i + 1 == size) {
      printf(" ");
      if ((i + 1) % 16 == 0) {
        printf("|  %s \n", ascii);
      } else if (i + 1 == size) {
        ascii[(i + 1) % 16] = '\0';
        if ((i + 1) % 16 <= 8) {
          printf(" ");
        }
        for (j = (i + 1) % 16; j < 16; ++j) {
          printf("   ");
        }
        printf("|  %s \n", ascii);
      }
    }
  }
}

__handler__ void empty_ph(handler_args_t *args) {
  printf("Payload handler!\n");
#if (NUM_INT_OP > 0)
  volatile int xx = 0;
  int x = xx;
  for (int i = 0; i < NUM_INT_OP; i++) {
    x = x * i;
  }
  xx = x;
#endif

  printf("Packet @ %p (L2: %p) (size %d)\n", args->task->pkt_mem,
         args->task->l2_pkt_mem, args->task->pkt_mem_size);
  hexdump(args->task->pkt_mem, args->task->pkt_mem_size);
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {NULL, empty_ph, NULL};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];

  *handler_mem_ptr = (void *)handler_mem;
}

