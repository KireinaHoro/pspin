#include "fpspin/fpspin.h"

#include <argp.h>
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

struct arguments {
  const char *pspin_dev;
  int dest_ctx;
  int max_sz;
  const char *out_file;
  const char *out_prefix;
  int expect;
};

static char doc[] =
    "SLMP file receiver\vReceive and optionally save a file transmitted in "
    "SLMP.  See the thesis for more information.";

static struct argp_option options[] = {
    {"device", 'd', "DEV_FILE", 0, "pspin device file"},
    {"ctx-id", 'x', "ID", 0, "destination fpspin execution context"},
    {"max-size", 'm', "NUM", 0, "maximum file size in bytes"},
    {"prefix", 'p', "STRING", 0,
     "prefix to saved files (will be in format <prefix>-<ID>.out)"},
    {"output", 'o', "FILE", 0, "output CSV file for measurements"},
    {"expect", 'e', "NUM", 0, "number of files to expect"},
    {0},
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *args = state->input;

  switch (key) {
  case 'd':
    args->pspin_dev = arg;
    break;
  case 'x':
    args->dest_ctx = atoi(arg);
    break;
  case 'p':
    args->out_prefix = arg;
    break;
  case 'm':
    args->max_sz = atoi(arg);
    break;
  case 'o':
    args->out_file = arg;
    break;
  case 'e':
    args->expect = atoi(arg);
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

volatile sig_atomic_t exit_flag = 0;
static void sigint_handler(int signum) { exit_flag = 1; }

static int file_id = 0;
#define PAGE_ROUND_UP(x)                                                       \
  ((((uint64_t)(x)) + PAGE_SIZE - 1) & (~(PAGE_SIZE - 1)))

int main(int argc, char *argv[]) {
  struct arguments args = {
      .pspin_dev = "/dev/pspin0",
      .dest_ctx = 0,
      .max_sz = PAGE_SIZE,
  };
  static struct argp argp = {options, parse_opt, NULL, doc};
  argp_program_version = "slmp 1.0";
  argp_program_bug_address = "Pengcheng Xu <pengxu@ethz.ch>";

  if (argp_parse(&argp, argc, argv, 0, 0, &args)) {
    return -1;
  }

  if (!args.out_prefix) {
    fprintf(stderr, "not saving received files (missing -p)\n");
  }
  args.max_sz = PAGE_ROUND_UP(args.max_sz);
  printf("Maximum file size: %d\n", args.max_sz);
  if (!args.expect) {
    fprintf(stderr, "running forever\n");
  }

  FILE *fp = NULL;
  if (!args.out_file) {
    fprintf(stderr, "not writing output file (missing -o)\n");
  } else {
    fp = fopen(args.out_file, "w");
    if (!fp) {
      perror("open out file");
      return EXIT_FAILURE;
    }
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

  fpspin_ctx_t ctx;
  fpspin_ruleset_t rs;
  fpspin_ruleset_slmp(&rs);

  // we are not using the "inline" area (that immediately follows the flag)
  if (!fpspin_init(&ctx, args.pspin_dev, __IMG__, args.dest_ctx, &rs, 1,
                   FPSPIN_HOSTDMA_PAGES_DEFAULT + args.max_sz / PAGE_SIZE)) {
    fprintf(stderr, "failed to initialise fpspin\n");
    goto fail;
  }

  fpspin_clear_counter(&ctx, 0); // cycles
  fpspin_clear_counter(&ctx, 1); // whole message
  fpspin_clear_counter(&ctx, 2); // per-packet (ph only)
  fpspin_clear_counter(&ctx, 3); // host DMA
  fpspin_clear_counter(&ctx, 4); // host notification
  fpspin_clear_counter(&ctx, 5); // head/tail (hh and th)

  int to_expect = args.expect ? args.expect : -1;
  while (true) {
    if (exit_flag) {
      printf("\nReceived SIGINT, exiting...\n");
      break;
    }
    for (int i = 0; i < NUM_HPUS; ++i) {
      fpspin_flag_t flag_to_host;
      fpspin_flag_t flag_from_host = {
          .len = 0,
      };

      // we calculate the payload offset ourselves
      if (!fpspin_pop_req(&ctx, i, &flag_to_host))
        continue;

      uint32_t file_len = flag_to_host.len;
      uint8_t *file_buf = (uint8_t *)ctx.cpu_addr + NUM_HPUS * PAGE_SIZE;
      // printf("Received file len: %d\n", file_len);

      if (args.out_prefix) {
        char filename_buf[FILENAME_MAX];
        filename_buf[FILENAME_MAX - 1] = 0;
        snprintf(filename_buf, sizeof(filename_buf) - 1, "%s-%d.out",
                 args.out_prefix, file_id++);
        FILE *fp = fopen(filename_buf, "wb");
        if (!fp) {
          perror("fopen");
          goto ack_file;
        }

        if (fwrite(file_buf, file_len, 1, fp) != 1) {
          perror("fwrite");
          goto ack_file;
        }
        fclose(fp);

        printf("Written file %s\n", filename_buf);
      } else {
        printf("Received file len=%d\n", file_len);
      }

    ack_file:
      fpspin_push_resp(&ctx, i, flag_from_host);

      if (to_expect != -1) {
        if (!--to_expect) {
          goto out;
        }
      }
    }
  }

out:;
  // get telemetry
  double cycles_avg = fpspin_get_cycles(&ctx, 0);
  fpspin_counter_t cc = fpspin_get_counter(&ctx, 0);
  double msg_avg = fpspin_get_cycles(&ctx, 1);
  fpspin_counter_t mc = fpspin_get_counter(&ctx, 1);
  double pkt_avg = fpspin_get_cycles(&ctx, 2);
  fpspin_counter_t pc = fpspin_get_counter(&ctx, 2);
  double host_avg = fpspin_get_cycles(&ctx, 3);
  fpspin_counter_t hc = fpspin_get_counter(&ctx, 3);
  double notification_avg = fpspin_get_cycles(&ctx, 4);
  fpspin_counter_t nc = fpspin_get_counter(&ctx, 4);
  double head_tail_avg = fpspin_get_cycles(&ctx, 5);
  fpspin_counter_t htc = fpspin_get_counter(&ctx, 5);

  fpspin_exit(&ctx);

  printf("Counters:\n");
  printf("... cycles: %lf cycles (sum %d, count %d)\n", cycles_avg, cc.sum,
         cc.count);
  printf("... msg: %lf cycles (sum %d, count %d)\n", msg_avg, mc.sum, mc.count);
  printf("... pkt: %lf cycles (sum %d, count %d)\n", pkt_avg, pc.sum, pc.count);
  printf("... host: %lf cycles (sum %d, count %d)\n", host_avg, hc.sum,
         hc.count);
  printf("... notification: %lf cycles (sum %d, count %d)\n", notification_avg,
         nc.sum, nc.count);
  printf("... head/tail: %lf cycles (sum %d, count %d)\n", head_tail_avg,
         htc.sum, htc.count);

  if (fp) {
    fprintf(fp, "cycles,msg,pkt,dma,notification,headtail\n");
    fprintf(fp, "%lf,%lf,%lf,%lf,%lf,%lf\n", cycles_avg, msg_avg, pkt_avg,
            host_avg, notification_avg, head_tail_avg);
    fclose(fp);
  }

  return EXIT_SUCCESS;

fail:
  fpspin_exit(&ctx);
  return EXIT_FAILURE;
}
