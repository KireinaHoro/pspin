#include "fpspin/fpspin.h"

#include <arpa/inet.h>
#include <assert.h>
#include <cblas.h>
#include <ctype.h>
#include <errno.h>
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../mpitypes/install/include/mpitypes_dataloop.h"
#include "../typebuilder/ddt_io_read.h"

#include "../handlers/datatype_descr.h"
#include "../handlers/datatypes.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define PSPIN_DEV "/dev/pspin0"
#define SIZE_MSG (128 * 1024 * 1024) // 128 MB
#define MSG_PAGES (SIZE_MSG / PAGE_SIZE)

#define NUM_ITERS 100
#define RTS_PORT 9331
#define TUNE_NUM_HITS 10
#define TUNE_DIM_START 1000
#define TUNE_DIM_STEP 10

typedef struct {
  int file_id;
  uint32_t userbuf_size;
  int num_received; // received number of messages so far

  int dim;          // dimension of dgemm matrices
  int num_iters;    // number of iterations in one trial / tune iteration
  int num_expected; // number of expected messages
  double *A, *B, *C;

  // sender IP and port
  in_addr_t sender;
  uint16_t rts_port;

  // performance measurements
  double dgemm_total;
  double rtt;
} datatypes_data_t;
static inline datatypes_data_t *to_data_ptr(fpspin_ctx_t *ctx) {
  return (datatypes_data_t *)ctx->app_data;
}

static int setup_datatypes_spin(fpspin_ctx_t *ctx, int dest_ctx,
                                char *type_descr, char *sender) {
  FILE *f = fopen(type_descr, "rb");
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

  // FIXME: assume count = 1
  int count = 1;

  printf("Count: %u\n", dtcount);
  printf("Blocks: %u\n", dtblocks);
  printf("Size: %li\n", dtinfo.size);
  printf("Extent: %li\n", dtinfo.extent);
  printf("True extent: %li\n", dtinfo.true_extent);
  printf("True LB: %li\n", dtinfo.true_lb);

  fpspin_ruleset_t rs;
  fpspin_ruleset_slmp(&rs);
  if (!fpspin_init(ctx, PSPIN_DEV, __IMG__, dest_ctx, &rs, 1)) {
    fprintf(stderr, "failed to initialise fpspin\n");
    return -1;
  }

  if (ctx->handler_mem.size < datatype_mem_size) {
    fprintf(stderr, "handler mem too small to fit datatype: %d vs %ld\n",
            ctx->handler_mem.size, datatype_mem_size);
    return -1;
  }

  if (ctx->mmap_len < PAGE_SIZE * (MSG_PAGES + NUM_HPUS)) {
    fprintf(stderr, "host dma area too small\n");
    return -1;
  }

  ctx->app_data = malloc(sizeof(datatypes_data_t));
  datatypes_data_t *app_data = to_data_ptr(ctx);
  app_data->userbuf_size =
      dtinfo.true_lb + MAX(dtinfo.extent, dtinfo.true_extent) * count;
  app_data->file_id = 0;
  app_data->num_received = 0;
  app_data->num_iters = NUM_ITERS;

  app_data->sender = inet_addr(sender);
  app_data->rts_port = RTS_PORT;

  app_data->A = app_data->B = app_data->C = NULL;

  // XXX: race condition: need to finish init before enabling HER

  // clear host buffer
  memset(ctx->cpu_addr, 0, ctx->mmap_len);

  // buffer area before copying onto the NIC
  size_t nic_buffer_size = sizeof(spin_datatype_mem_t) + datatype_mem_size +
                           sizeof(spin_core_state_t) * NUM_HPUS;
  void *nic_buffer = malloc(nic_buffer_size);
  if (!nic_buffer) {
    fprintf(stderr, "failed to allocate nic buffer\n");
    exit(EXIT_FAILURE);
  }

  fpspin_addr_t nic_mem_base = ctx->handler_mem.addr;
  // layout: spin_datatype_mem_t | ddt | spin_core_state_t
  size_t nic_ddt_off = sizeof(spin_datatype_mem_t);

  fpspin_addr_t nic_ddt_pos = nic_mem_base + nic_ddt_off;
  uint8_t *nic_buffer_ddt_data = (uint8_t *)nic_buffer + nic_ddt_off;

  // relocate datatype for NIC
  memcpy(nic_buffer_ddt_data, datatype_mem_ptr_raw, datatype_mem_size);
  remap_spin_datatype(nic_buffer_ddt_data, datatype_mem_size, nic_ddt_pos,
                      true);

  spin_datatype_mem_t *nic_buffer_ddt_descr = (spin_datatype_mem_t *)nic_buffer;
  spin_datatype_t *nic_buffer_dt = (spin_datatype_t *)nic_buffer_ddt_data;

  spin_core_state_t *nic_buffer_state =
      (spin_core_state_t *)(nic_buffer_ddt_data + datatype_mem_size);
  nic_buffer_ddt_descr->state =
      (spin_core_state_t *)(nic_ddt_pos + datatype_mem_size);

  for (int i = 0; i < NUM_HPUS; ++i) {
    // TODO: HPU-local: each HPU has its own copy of the segment
    nic_buffer_state[i].state = nic_buffer_dt->seg;

    nic_buffer_state[i].params = nic_buffer_dt->params;
  }

  // copy to NIC memory
  fpspin_write_memory(ctx, nic_mem_base, nic_buffer, nic_buffer_size);
  free(nic_buffer);
  free(datatype_mem_ptr_raw);

  return 0;
}

static void finish_datatypes_spin(fpspin_ctx_t *ctx) {
  free(ctx->app_data);
  fpspin_exit(ctx);
}

// query all cores one round to poll for an incoming message
static bool query_once(fpspin_ctx_t *ctx, int num_expected) {
  datatypes_data_t *app_data = to_data_ptr(ctx);

  for (int i = 0; i < NUM_HPUS; ++i) {
    fpspin_flag_t flag_to_host;
    if (!fpspin_pop_req(ctx, i, &flag_to_host))
      continue;
    ++app_data->num_received;

    // got finished datatype from PsPIN
    fpspin_flag_t flag_from_host = {.len = 0};

    // write received message to file
    // we have fixed size userbuf from datatypes desc
    uint8_t *msg_buf = (uint8_t *)ctx->cpu_addr + NUM_HPUS * PAGE_SIZE;

    char filename_buf[FILENAME_MAX];
    filename_buf[FILENAME_MAX - 1] = 0;
    snprintf(filename_buf, sizeof(filename_buf) - 1, "recv_%d.out",
             app_data->file_id++);
    FILE *fp = fopen(filename_buf, "wb");
    if (!fp) {
      perror("fopen");
      goto ack_file;
    }

    if (fwrite(msg_buf, app_data->userbuf_size, 1, fp) != 1) {
      perror("fwrite");
      goto ack_file;
    }
    fclose(fp);

    printf("Written file %s\n", filename_buf);
    memset(msg_buf, 0, app_data->userbuf_size);

  ack_file:
    fpspin_push_resp(ctx, i, flag_from_host);
  }

  return app_data->num_received == num_expected;
}

static inline double randlf(double fmin, double fmax) {
  double f = (double)rand() / RAND_MAX;
  return fmin + f * (fmax - fmin);
}

static inline double curtime() {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec + now.tv_nsec * 1e-9;
}

void setup_trial(fpspin_ctx_t *ctx, int dim, int num_expected) {
  datatypes_data_t *app_data = to_data_ptr(ctx);

  app_data->dim = dim;
  app_data->num_expected = num_expected;

  if (app_data->A) free(app_data->A);
  if (app_data->B) free(app_data->B);
  if (app_data->C) free(app_data->C);

  long sz = dim * dim;
  app_data->A = malloc(sizeof(double) * sz);
  app_data->B = malloc(sizeof(double) * sz);
  app_data->C = malloc(sizeof(double) * sz);

  // random initialise matrices
  for (int i = 0; i < sz; ++i) {
    app_data->A[i] = app_data->B[i] = app_data->C[i] = randlf(-100.0, 100.0);
  }
}

void send_rts(fpspin_ctx_t *ctx) {
  datatypes_data_t *app_data = to_data_ptr(ctx);

  // TODO: send RTS over UDP that contains num_expected to sender
  // app_data->sender
  // app_data->rts_port
}

// dim: dimension of dgemm
// num_iters: number of iterations
// return value: -1 if dgemm too long, 1 if too short; 0 if right on time
int run_trial(fpspin_ctx_t *ctx) {
  datatypes_data_t *app_data = to_data_ptr(ctx);
  int dim = app_data->dim;
  int ret = 1;

  // RTS to sender - start of RTT
  send_rts(ctx);
  double rtt_start = curtime();

  for (int i = 0; i < app_data->num_iters; ++i) {
    double cpu_start = curtime();
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, dim, dim, dim, 1.0,
                app_data->A, dim, app_data->B, dim, 2, app_data->C, dim);
    app_data->dgemm_total += curtime() - cpu_start;

    if (query_once(ctx, app_data->num_expected)) {
      if (i == app_data->num_iters - 1) {
        // right on time
        ret = 0;
        goto finish;
      } else {
        // dgemm too long / datatypes too fast
        ret = -1;
        goto finish;
      }
    }
  }

  // too slow - fetch the message and return
  while (!query_once(ctx, app_data->num_expected))
    ;
  app_data->num_received = 0;
  ret = 1;

finish:
  // RTT finish
  app_data->rtt = curtime() - rtt_start;

  printf("DGEMM total: %lf; RTT: %lf\n", app_data->dgemm_total, app_data->rtt);
  return ret;
}

int main(int argc, char *argv[]) {
  if (argc != 5) {
    fprintf(stderr, "usage: %s <ctx id> <type descr> <sender ip> <num msg>\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }

  int dest_ctx = atoi(argv[1]);
  int num_expected = atoi(argv[4]);
  fpspin_ctx_t ctx;
  if (setup_datatypes_spin(&ctx, dest_ctx, argv[2], argv[3]))
    goto fail;

  // tune dgemm size
  int dim = TUNE_DIM_START;
  int hit = 0;
  while (hit < TUNE_NUM_HITS) {
    setup_trial(&ctx, dim, num_expected);
    int res = run_trial(&ctx);
    if (!res) {
      printf("Trial dim=%d hit!\n", dim);
      ++hit;
    } else {
      if (res > 0) {
        printf("Trial dim=%d dgemm too short\n", dim);
        dim += TUNE_DIM_STEP;
      } else {
        printf("Trial dim=%d dgemm too long\n", dim);
        dim -= TUNE_DIM_STEP;
      }
      hit = 0;
    }
  }

  // run trial
  printf("\n==> Tuned trial run:\n");
  setup_trial(&ctx, dim, num_expected);
  run_trial(&ctx);

  // get telemetry from pspin
  fpspin_counter_t pkt_counter = fpspin_get_counter(&ctx, 0);
  fpspin_counter_t msg_counter = fpspin_get_counter(&ctx, 1);

  printf("Counters:\n");
  printf("... pkt: %f cycles (%d iters)\n",
         (float)pkt_counter.sum / pkt_counter.count, pkt_counter.count);
  printf("... msg: %f cycles (%d iters)\n",
         (float)msg_counter.sum / msg_counter.count, msg_counter.count);

  finish_datatypes_spin(&ctx);

  // TODO: write measurements to CSV

  return EXIT_SUCCESS;

fail:
  finish_datatypes_spin(&ctx);
  return EXIT_FAILURE;
}
