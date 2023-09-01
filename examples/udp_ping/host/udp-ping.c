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

static char doc[] = "UDP ping-pong server host program\vResponds to UDP "
                    "packets as ping-pong in FPsPIN, by sending the original "
                    "payload back.  See the thesis for more information.";

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

#define PSPIN_DEV "/dev/pspin0"

volatile sig_atomic_t exit_flag = 0;
static void sigint_handler(int signum) { exit_flag = 1; }

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

int main(int argc, char *argv[]) {
  struct arguments args = {
      .pspin_dev = "/dev/pspin0",
      .dest_ctx = 0,
  };
  static struct argp argp = {options, parse_opt, NULL, doc};
  argp_program_version = "udp-ping 1.0";
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
  fpspin_ruleset_udp(&rs);
  if (!fpspin_init(&ctx, PSPIN_DEV, __IMG__, args.dest_ctx, &rs, 1,
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
      volatile pkt_hdr_t *hdrs = (pkt_hdr_t *)pkt_addr;
      volatile uint8_t *payload = (uint8_t *)hdrs + sizeof(pkt_hdr_t);

      // uint16_t dma_len = FLAG_LEN(flag_to_host);
      uint16_t udp_len = ntohs(hdrs->udp_hdr.length);
      uint16_t payload_len = udp_len - sizeof(udp_hdr_t);

      // printf("Received packet on HPU %d, udp_len=%d\n", i, udp_len);

      // recalculate lengths
      uint16_t ul_host = payload_len + sizeof(udp_hdr_t);
      uint16_t il_host = sizeof(ip_hdr_t) + ul_host;
      uint16_t return_len = il_host + sizeof(eth_hdr_t);
      hdrs->udp_hdr.length = htons(ul_host);
      hdrs->udp_hdr.checksum = 0;
      hdrs->ip_hdr.length = htons(il_host);
      hdrs->ip_hdr.checksum = 0;
      hdrs->ip_hdr.checksum =
          ip_checksum((uint8_t *)&hdrs->ip_hdr, sizeof(ip_hdr_t));

      // printf("Return packet: %d bytes\n", return_len);
      // hexdump(pkt_addr, return_len);

      fpspin_push_resp(&ctx, i, (fpspin_flag_t){.len = return_len});

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
