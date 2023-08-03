// Copyright 2022 ETH Zurich
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

#define GDRIVER_OK 0
#define GDRIVER_ERR 1

#define GDRIVER_MATCHING_CTX_MAXSIZE 256

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include "spin.h"

typedef uint32_t (*fill_packet_fun_t)(uint32_t, uint32_t, uint8_t*, uint32_t, uint32_t*);
// *msg_idx, *is_eom -> len
typedef void (*interactive_feedback_fun_t)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef int (*match_packet_fun_t)(char*, char*);

int gdriver_add_ectx(const char *hfile, const char *hh, const char *ph, const char *th,
    fill_packet_fun_t fill_cb, void *l2_img, size_t l2_img_size,
    void *matching_ctx, size_t matching_ctx_size);
spin_ec_t *gdriver_get_ectx_mems();
int gdriver_run();
int gdriver_fini();
int gdriver_init(int argc, char **argv, match_packet_fun_t matching_cb, int *ectx_num);

bool gdriver_is_interactive();
void gdriver_set_interactive_cb(interactive_feedback_fun_t cb);