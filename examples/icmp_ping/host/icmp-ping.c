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
#include <time.h>
#include <unistd.h>

struct arguments {
  const char *pspin_dev;
  int dest_ctx;
  const char *out_file;
  int expect;
  int expect_second;
};

static char doc[] = "ICMP ping-pong server host program\vResponds to ICMP Echo "
                    "messages in FPsPIN.  See the thesis for more information.";

static struct argp_option options[] = {
    {"device", 'd', "DEV_FILE", 0, "pspin device file"},
    {"ctx-id", 'x', "ID", 0, "destination fpspin execution context"},
    {"output", 'o', "FILE", 0, "output CSV file for measurements"},
    {"expect", 'e', "NUM", 0, "number of packets to expect before exiting"},
    {"second", 's', "NUM", 0, "number of seconds before exiting"},
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
  case 'o':
    args->out_file = arg;
    break;
  case 'e':
    args->expect = atoi(arg);
    break;
  case 's':
    args->expect_second = atoi(arg);
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

volatile sig_atomic_t exit_flag = 0;
static void sigint_handler(int signum) { exit_flag = 1; }

// FIXME: only for ICMP echo
typedef struct {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint32_t rest_of_header;
} icmp_hdr_t;

typedef struct {
  eth_hdr_t eth_hdr;
  ip_hdr_t ip_hdr; // FIXME: assumes ihl=4
  icmp_hdr_t icmp_hdr;
} __attribute__((__packed__)) hdr_t;

// http://www.microhowto.info/howto/calculate_an_internet_protocol_checksum_in_c.html
uint16_t ip_checksum(void *vdata, size_t length) {
  // Cast the data pointer to one that can be indexed.
  char *data = (char *)vdata;

  // Initialise the accumulator.
  uint32_t acc = 0xffff;

  // Handle complete 16-bit blocks.
  for (size_t i = 0; i + 1 < length; i += 2) {
    uint16_t word;
    memcpy(&word, data + i, 2);
    acc += ntohs(word);
    if (acc > 0xffff) {
      acc -= 0xffff;
    }
  }

  // Handle any partial block at the end of the data.
  if (length & 1) {
    uint16_t word = 0;
    memcpy(&word, data + length - 1, 1);
    acc += ntohs(word);
    if (acc > 0xffff) {
      acc -= 0xffff;
    }
  }

  // Return the checksum in network byte order.
  return htons(~acc);
}

void ruleset_icmp_echo(fpspin_ruleset_t *rs) {
  assert(NUM_RULES_PER_RULESET == 4);
  *rs = (fpspin_ruleset_t){
      .mode = FPSPIN_MODE_AND,
      .r =
          {
              FPSPIN_RULE_IP,
              FPSPIN_RULE_IP_PROTO(1), // ICMP
              ((struct fpspin_rule){.idx = 8,
                                    .mask = 0xff00,
                                    .start = 0x0800,
                                    .end = 0x0800}), // ICMP Echo-Request
              FPSPIN_RULE_FALSE,                     // never EOM
          },
  };
}

int main(int argc, char *argv[]) {
  struct arguments args = {
      .pspin_dev = "/dev/pspin0",
      .dest_ctx = 0,
  };
  static struct argp argp = {options, parse_opt, NULL, doc};
  argp_program_version = "icmp-ping 1.0";
  argp_program_bug_address = "Pengcheng Xu <pengxu@ethz.ch>";

  if (argp_parse(&argp, argc, argv, 0, 0, &args)) {
    return -1;
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
  if (!args.expect) {
    fprintf(stderr,
            "no number of packets to expect set (-e), running indefinitely\n");
  }
  if (args.expect_second) {
    fprintf(stderr, "running for %d seconds\n", args.expect_second);
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
  // custom ruleset
  ruleset_icmp_echo(&rs);
  if (!fpspin_init(&ctx, args.pspin_dev, __IMG__, args.dest_ctx, &rs, 1,
                   FPSPIN_HOSTDMA_PAGES_DEFAULT)) {
    fprintf(stderr, "failed to initialise fpspin\n");
    goto fail;
  }

  fpspin_clear_counter(&ctx, 0); // handler cycles
  fpspin_clear_counter(&ctx, 1); // host dma cycles
  fpspin_clear_counter(&ctx, 2); // cycles

  int to_expect = args.expect ? args.expect : -1;
  clock_t started = clock();

  while (true) {
    if (exit_flag) {
      printf("\nReceived SIGINT, exiting...\n");
      break;
    }
    for (int i = 0; i < NUM_HPUS; ++i) {
      if (args.expect_second &&
          (double)(clock() - started) / CLOCKS_PER_SEC > args.expect_second) {
        goto out;
      }

      fpspin_flag_t flag_to_host;
      volatile uint8_t *pkt_addr;

      if (!(pkt_addr = fpspin_pop_req(&ctx, i, &flag_to_host)))
        continue;
      volatile hdr_t *hdrs = (hdr_t *)pkt_addr;
      uint16_t ip_len = ntohs(hdrs->ip_hdr.length);
      uint16_t eth_len = sizeof(eth_hdr_t) + ip_len;
      uint16_t flag_len = flag_to_host.len;
      if (flag_len < eth_len) {
        printf("Warning: packet truncated; received %d, expected %d (from IP "
               "header)\n",
               flag_len, eth_len);
      }

      // ICMP type and checksum
      size_t icmp_len = ip_len - sizeof(ip_hdr_t);
      hdrs->icmp_hdr.type = 0; // Echo-Reply
      hdrs->icmp_hdr.checksum = 0;
      hdrs->icmp_hdr.checksum =
          ip_checksum((uint8_t *)&hdrs->icmp_hdr, icmp_len);

      fpspin_push_resp(&ctx, i, (fpspin_flag_t){.len = eth_len});

      if (to_expect != -1) {
        if (!--to_expect) {
          goto out;
        }
      }
    }
  }

out:;
  // get telemetry
  double handler_avg = fpspin_get_cycles(&ctx, 0);
  double host_dma_avg = fpspin_get_cycles(&ctx, 1);
  double cycles_avg = fpspin_get_cycles(&ctx, 2);

  fpspin_exit(&ctx);

  printf("Counters:\n");
  printf("... handler: %lf cycles\n", handler_avg);
  printf("... host_dma: %lf cycles\n", host_dma_avg);
  printf("... cycles: %lf cycles\n", cycles_avg);

  if (fp) {
    fprintf(fp, "handler,host_dma,cycles\n");
    fprintf(fp, "%lf,%lf,%lf\n", handler_avg, host_dma_avg, cycles_avg);
    fclose(fp);
  }

  return EXIT_SUCCESS;

fail:
  fpspin_exit(&ctx);
  return EXIT_FAILURE;
}
