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
#include <spin_host.h>

__handler__ void slmp_hh(handler_args_t *args) {
  task_t *task = args->task;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  printf("New message: flow id %d, offset %d, payload size %d\n", task->flow_id,
         hdrs->slmp_hdr.pkt_off, SLMP_PAYLOAD_LEN(hdrs));
}

__handler__ void slmp_th(handler_args_t *args) {
  task_t *task = args->task;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  printf("End of message: flow id %d, offset %d, payload size %d\n", task->flow_id,
         hdrs->slmp_hdr.pkt_off, SLMP_PAYLOAD_LEN(hdrs));
}

__handler__ void slmp_ph(handler_args_t *args) {
  task_t *task = args->task;

  slmp_pkt_hdr_t *hdrs = (slmp_pkt_hdr_t *)(task->pkt_mem);
  uint16_t pkt_len = args->task->pkt_mem_size;

  printf("Payload: flow id %d, offset %d, payload size %d\n", task->flow_id,
         hdrs->slmp_hdr.pkt_off, SLMP_PAYLOAD_LEN(hdrs));
}

void init_handlers(handler_fn *hh, handler_fn *ph, handler_fn *th,
                   void **handler_mem_ptr) {
  volatile handler_fn handlers[] = {slmp_hh, slmp_ph, slmp_th};
  *hh = handlers[0];
  *ph = handlers[1];
  *th = handlers[2];
}
