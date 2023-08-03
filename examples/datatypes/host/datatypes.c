#include <argp.h>
#include <arpa/inet.h>
#include <assert.h>
#include <cblas.h>
#include <ctype.h>
#include <errno.h>
#include <immintrin.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../include/datatypes_host.h"

struct arguments {
  const char *pspin_dev;
  int dest_ctx;
  enum {
    MODE_VERIFY,
    MODE_BENCHMARK,
  } mode;
  const char *out_file;
  // benchmark mode
  int num_iterations;
  int num_misses;
  double dgemm_margin;
  int num_trials;
  int num_parallel_msgs;
  in_addr_t rts_server;
  int rts_port;
  int tune_num_hits;
  int tune_dim_start;
  int tune_dim_step;
  // arguments
  const char *type_descr_file;
};

#define SIZE_MSG (128 * 1024 * 1024) // 128 MB
#define MSG_PAGES (SIZE_MSG / PAGE_SIZE)

static char doc[] =
    "Datatypes receiver host program -- verify or benchmark "
    "the PsPIN datatypes implementation\vRun the datatypes PsPIN "
    "implementation in verify mode or benchmark mode.  In verify mode, we "
    "receive a message from the sender and writes the received userbuf to a "
    "file.  This file can be later compared with a golden userbuf "
    "output generated by the sender through direct invocation of "
    "MPITypes.  In benchmark mode, we tune the dgemm workload and produce "
    "performance measurements.  Refer to the thesis for more information.";
static char args_doc[] = "TYPE_DESCR_BIN";

static struct argp_option options[] = {
    {0, 0, 0, 0, "General options:"},
    {"device", 'd', "DEV_FILE", 0, "pspin device file"},
    {"ctx-id", 'x', "ID", 0, "destination fpspin execution context ID"},
    {"output", 'o', "FILE", 0,
     "output file (userbuf dump in verification mode, performance data CSV in "
     "benchmark mode)"},
    {"rts-server", 'q', "IP4", 0, "IP address to send RTS to sender"},
    {"rts-port", 'r', "PORT", 0, "UDP port to send RTS to sender"},

    {0, 0, 0, 0, "Verification options:"},
    {"verify", 'v', 0, 0, "run verification"},

    {0, 0, 0, 0, "Benchmark options:"},
    {"iterations", 'i', "NUM", 0,
     "number of dgemm-poll iterations in one trial"},
    {"misses", 'm', "NUM", 0, "number of misses allowed for a right match"},
    {"dgemm-err-margin", 'g', "FLOAT", 0,
     "margin of dgemm validation (default 0.6)"},
    {"trials", 't', "NUM", 0,
     "number of trials in the measurement after tuning"},
    {"parallel-msgs", 'p', "NUM", 0,
     "number of messages to send in parallel at the sender"},

    {0, 0, 0, 0, "Benchmark tuning options:"},
    {"hits", 'h', "NUM", 0, "number of hits required for tune to succeed"},
    {"dim-begin", 'b', "NUM", 0, "start of dgemm matrix dimension"},
    {"dim-step", 's', "NUM", 0, "increment of dgemm matrix dimension"},
    {0}};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *args = state->input;

  switch (key) {
  case 'd':
    args->pspin_dev = arg;
    break;
  case 'x':
    args->dest_ctx = atoi(arg);
    break;
  case 'v':
    args->mode = MODE_VERIFY;
    break;
  case 'o':
    args->out_file = arg;
    break;
  case 'i':
    args->num_iterations = atoi(arg);
    break;
  case 'm':
    args->num_misses = atoi(arg);
    break;
  case 'g':
    args->dgemm_margin = atof(arg);
    break;
  case 't':
    args->num_trials = atoi(arg);
    break;
  case 'q':
    args->rts_server = inet_addr(arg);
    break;
  case 'r':
    args->rts_port = atoi(arg);
    break;
  case 'p':
    args->num_parallel_msgs = atoi(arg);
    break;
  case 'h':
    args->tune_num_hits = atoi(arg);
    break;
  case 'b':
    args->tune_dim_start = atoi(arg);
    break;
  case 's':
    args->tune_dim_step = atoi(arg);
    break;
  case ARGP_NO_ARGS:
    argp_usage(state);
    break;
  case ARGP_KEY_ARG:
    if (state->arg_num >= 1)
      argp_usage(state);
    args->type_descr_file = arg;
    break;
  case ARGP_KEY_END:
    if (state->arg_num < 1)
      argp_usage(state);
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

typedef struct {
  int file_id;
  struct arguments args;
  uint32_t userbuf_size;
  int num_received;      // received but unclaimed (from parallel messages)
  uint32_t num_elements; // number of datatype elements, from DDT bin

  int dim; // dimension of dgemm matrices - current trial
  double *A, *B, *C;

  // performance measurements
  double dgemm_total;
  double rtt;

  // RTS socket
  int rts_sockfd;
} datatypes_data_t;
static inline datatypes_data_t *to_data_ptr(fpspin_ctx_t *ctx) {
  return (datatypes_data_t *)ctx->app_data;
}

static int setup_datatypes_spin(fpspin_ctx_t *ctx, int argc, char *argv[]) {
  ctx->app_data = malloc(sizeof(datatypes_data_t));
  datatypes_data_t *app_data = to_data_ptr(ctx);
  app_data->file_id = 0;
  app_data->num_received = 0;

  app_data->A = app_data->B = app_data->C = NULL;

  app_data->args = (struct arguments){
      .pspin_dev = "/dev/pspin0",
      .dest_ctx = 0,
      .mode = MODE_BENCHMARK,
      .num_iterations = 100,
      .num_misses = 5,
      .dgemm_margin = 0.6,
      .num_trials = 10,
      .num_parallel_msgs = 1,
      .rts_port = RTS_PORT,
      .tune_num_hits = 10,
      .tune_dim_start = 1000,
      .tune_dim_step = 10,
  };

  static struct argp argp = {options, parse_opt, args_doc, doc};
  argp_program_version = "datatypes 1.0";
  argp_program_bug_address = "Pengcheng Xu <pengxu@ethz.ch>";

  if (argp_parse(&argp, argc, argv, 0, 0, &app_data->args)) {
    return -1;
  }
  struct arguments *args = &app_data->args;

  // check arguments
  if (!args->out_file) {
    fprintf(stderr, "error: no output file specified (see --help)\n");
    return -1;
  }
  if (!args->rts_server) {
    fprintf(stderr, "error: no sender rts IP address specified (see --help)\n");
    return -1;
  }

  fpspin_ruleset_t rs;
  fpspin_ruleset_slmp(&rs);
  if (!fpspin_init(ctx, args->pspin_dev, __IMG__, args->dest_ctx, &rs, 1)) {
    fprintf(stderr, "failed to initialise fpspin\n");
    return -1;
  }

  if (ctx->mmap_len < PAGE_SIZE * (MSG_PAGES + NUM_HPUS)) {
    fprintf(stderr, "host dma area too small\n");
    return -1;
  }

  // XXX: race condition: need to finish init before enabling HER

  // clear host buffer
  memset(ctx->cpu_addr, 0, ctx->mmap_len);

  fpspin_addr_t nic_mem_base = ctx->handler_mem.addr;
  void *nic_buffer;
  size_t nic_buffer_size = 0;
  void *datatype_mem_ptr_raw = prepare_ddt_nicmem(
      args->type_descr_file, ctx->handler_mem, &nic_buffer, &nic_buffer_size,
      &app_data->num_elements, &app_data->userbuf_size);
  printf("Userbuf size: %d\n", app_data->userbuf_size);
  if (!nic_buffer_size) {
    // ddt remap failed
    return -1;
  }

  // copy to NIC memory
  fpspin_write_memory(ctx, nic_mem_base, nic_buffer, nic_buffer_size);
  free(nic_buffer);
  free(datatype_mem_ptr_raw);

  // RTS socket
  app_data->rts_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (app_data->rts_sockfd < 0) {
    perror("open rts socket");
    return -1;
  }

  return 0;
}

static void finish_datatypes_spin(fpspin_ctx_t *ctx) {
  datatypes_data_t *app_data = to_data_ptr(ctx);
  free(app_data->A);
  free(app_data->B);
  free(app_data->C);
  close(app_data->rts_sockfd);
  free(app_data);

  // wait until all stdout are flushed
  printf("Waiting for stdout flush...\n");
  sleep(2);
  fpspin_exit(ctx);
}

typedef void (*msg_handler_t)(fpspin_ctx_t *, uint8_t *);
// query all cores one round to poll for an incoming message
static bool query_once(fpspin_ctx_t *ctx, msg_handler_t func) {
  datatypes_data_t *app_data = to_data_ptr(ctx);
  int num_req = app_data->args.num_parallel_msgs;

  if (app_data->num_received >= num_req) {
    app_data->num_received -= num_req;
    return true;
  }

  for (int i = 0; i < NUM_HPUS; ++i) {
    fpspin_flag_t flag_to_host;
    if (!fpspin_pop_req(ctx, i, &flag_to_host))
      continue;

    // repurposed len for msgid report
    int msgid = flag_to_host.len;
    printf("Received message #%u id=%u hpu=%d\n", msgid, flag_to_host.dma_id,
           flag_to_host.hpu_id);

    ++app_data->num_received;

    // got finished datatype from PsPIN
    fpspin_flag_t flag_from_host = {.len = 0};

    // write received message to file
    // we have fixed size userbuf from datatypes desc
    uint8_t *msg_buf = (uint8_t *)ctx->cpu_addr + NUM_HPUS * PAGE_SIZE;

    if (func) {
      func(ctx, msg_buf);
    }

    // clear buffer
    memset(msg_buf, 0, app_data->userbuf_size);
    fpspin_push_resp(ctx, i, flag_from_host);
  }

  return false;
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

void init_matrices(double **A, double **B, double **C, int dim) {
  long sz = dim * dim;
  *A = malloc(sizeof(double) * sz);
  *B = malloc(sizeof(double) * sz);
  *C = malloc(sizeof(double) * sz);

  // random initialise matrices
  for (int i = 0; i < sz; ++i) {
    (*A)[i] = (*B)[i] = (*C)[i] = randlf(-100.0, 100.0);
  }
}

void setup_trial(fpspin_ctx_t *ctx, int dim) {
  datatypes_data_t *app_data = to_data_ptr(ctx);

  app_data->dim = dim;

  if (app_data->A)
    free(app_data->A);
  if (app_data->B)
    free(app_data->B);
  if (app_data->C)
    free(app_data->C);

  init_matrices(&app_data->A, &app_data->B, &app_data->C, dim);
}

void send_rts(fpspin_ctx_t *ctx) {
  datatypes_data_t *app_data = to_data_ptr(ctx);
  struct arguments *args = &app_data->args;

  datatypes_rts_t rts = {
      .num_parallel_msgs = args->num_parallel_msgs,
      .elem_count = app_data->num_elements,
  };

  struct sockaddr_in server = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = args->rts_server,
      .sin_port = htons(args->rts_port),
  };

  if (sendto(app_data->rts_sockfd, &rts, sizeof(rts), 0,
             (const struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("sendto");
  }

  char str[40];
  inet_ntop(AF_INET, &args->rts_server, str, sizeof(str));

  printf("Sent RTS to %s: %d datatypes, %d parallel msgs\n", str,
         rts.elem_count, rts.num_parallel_msgs);
}

// return value: -1 if dgemm too long, 1 if too short; 0 if right on time
int run_trial(fpspin_ctx_t *ctx) {
  datatypes_data_t *app_data = to_data_ptr(ctx);
  struct arguments *args = &app_data->args;
  int dim = app_data->dim;
  int ret = 1;

  // RTS to sender - start of RTT
  send_rts(ctx);
  double rtt_start = curtime();

  for (int i = 0; i < args->num_iterations; ++i) {
    double cpu_start = curtime();
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, dim, dim, dim, 1.0,
                app_data->A, dim, app_data->B, dim, 2, app_data->C, dim);
    app_data->dgemm_total += curtime() - cpu_start;

    if (query_once(ctx, NULL)) {
      if (i >= args->num_iterations - 1 - args->num_misses) {
        // within the miss requirement
        printf("On time (i=%d)!\n", i);
        ret = 0;
        goto finish;
      } else {
        // dgemm too long / datatypes too fast
        printf("Dgemm too long! i=%d\n", i);
        ret = -1;
        goto finish;
      }
    }
  }

  printf("Dgemm too short!\n");

  // too slow - fetch the message and return
  while (!query_once(ctx, NULL))
    ;
  app_data->num_received = 0;
  ret = 1;

finish:
  // RTT finish
  app_data->rtt = curtime() - rtt_start;

  printf("DGEMM total: %lf; RTT: %lf\n", app_data->dgemm_total, app_data->rtt);
  return ret;
}

bool check_dgemm(struct arguments *args) {
  const long dim = 2000;
  const int iters = 50;
  double flops = 2 * dim * dim * dim;
  printf("Checking DGEMM speed with dim=%ld...\n", dim);

  double *A, *B, *C;
  init_matrices(&A, &B, &C, dim);

  // fpga1: Ryzen 7 2700 (Zen+), AVX2 VFMADD132PD (2*4 FLOP)
  //        reciprocal latency=1
  //        typical frequency 3.4 GHz, 8 cores
  double theo_gflops = 3.4 * 2 * 4 * 8;
  double start = curtime();

  for (int i = 0; i < iters; ++i)
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, dim, dim, dim, 1.0,
                A, dim, B, dim, 2, C, dim);

  double elapsed = (curtime() - start) / iters;
  double real_gflops = flops / elapsed / 1e9;

  printf("Theoretical double GFLOPS: %lf; measured double GFLOPS: %lf\n",
         theo_gflops, real_gflops);

  bool pass = true;
  if (real_gflops / theo_gflops >= args->dgemm_margin) {
    printf("PASS margin requirement %lf\n", args->dgemm_margin);
  } else {
    printf("FAIL margin requirement %lf\n", args->dgemm_margin);
    pass = false;
  }

  free(A);
  free(B);
  free(C);

  return pass;
}

volatile sig_atomic_t exit_flag = 0;
static void sigint_handler(int signum) { exit_flag = 1; }

static void dump_userbuf(fpspin_ctx_t *ctx, uint8_t *msg_buf) {
  datatypes_data_t *app_data = to_data_ptr(ctx);
  FILE *fp = fopen(app_data->args.out_file, "wb");
  if (!fp) {
    perror("fopen");
    return;
  }

  if (fwrite(msg_buf, app_data->userbuf_size, 1, fp) != 1) {
    perror("fwrite");
    return;
  }
  fclose(fp);

  printf("Written userbuf dump to %s\n", app_data->args.out_file);
}

int main(int argc, char *argv[]) {
  fpspin_ctx_t ctx;
  if (setup_datatypes_spin(&ctx, argc, argv))
    goto fail;
  datatypes_data_t *app_data = to_data_ptr(&ctx);
  struct arguments *args = &app_data->args;

  if (args->mode == MODE_VERIFY) {
    // setup signal handler
    struct sigaction sa = {
        .sa_handler = sigint_handler,
        .sa_flags = 0,
    };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL)) {
      perror("sigaction");
      goto fail;
    }

    // RTS for one message
    send_rts(&ctx);

    // receive one message and exit
    while (true) {
      if (query_once(&ctx, &dump_userbuf))
        break;
      if (exit_flag) {
        fprintf(stderr, "Received SIGINT, exiting...\n");
        break;
      }
    }
  } else {
    // check dgemm intensity
    if (!check_dgemm(args)) {
      goto fail;
    }

    // tune dgemm size
    int dim = args->tune_dim_start;
    int hit = 0;
    while (hit < args->tune_num_hits) {
      printf("Running trial dim=%d...\n", dim);
      setup_trial(&ctx, dim);
      int res = run_trial(&ctx);
      if (!res) {
        printf("Trial dim=%d hit!\n", dim);
        ++hit;
      } else {
        if (res > 0) {
          printf("Trial dim=%d dgemm too short\n", dim);
          dim += args->tune_dim_step;
        } else {
          printf("Trial dim=%d dgemm too long\n", dim);
          dim -= args->tune_dim_step;
        }
        hit = 0;
      }
      app_data->dgemm_total = 0;
    }

    // run trial
    printf("\n==> Tuned trial run:\n");
    setup_trial(&ctx, dim);
    run_trial(&ctx);

    // TODO: write measurements to CSV
  }

  // get telemetry from pspin
  fpspin_counter_t pkt_counter = fpspin_get_counter(&ctx, 0);
  fpspin_counter_t msg_counter = fpspin_get_counter(&ctx, 1);

  printf("Counters:\n");
  printf("... pkt: %f cycles (%d iters)\n",
         (float)pkt_counter.sum / pkt_counter.count, pkt_counter.count);
  printf("... msg: %f cycles (%d iters)\n",
         (float)msg_counter.sum / msg_counter.count, msg_counter.count);

  finish_datatypes_spin(&ctx);

  return EXIT_SUCCESS;

fail:
  finish_datatypes_spin(&ctx);
  return EXIT_FAILURE;
}
