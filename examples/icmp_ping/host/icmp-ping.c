#include "fpspin/fpspin.h"

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

#define PSPIN_DEV "/dev/pspin0"

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
  if (argc != 2) {
    fprintf(stderr, "usage: %s <ctx id>\n", argv[0]);
    exit(EXIT_FAILURE);
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

  int dest_ctx = atoi(argv[1]);
  fpspin_ctx_t ctx;
  fpspin_ruleset_t rs;
  // custom ruleset
  ruleset_icmp_echo(&rs);
  if (!fpspin_init(&ctx, PSPIN_DEV, __IMG__, dest_ctx, &rs, 1)) {
    fprintf(stderr, "failed to initialise fpspin\n");
    goto fail;
  }

  // loading finished - application logic from here
  while (true) {
    if (exit_flag) {
      printf("\nReceived SIGINT, exiting...\n");
      break;
    }
    for (int i = 0; i < NUM_HPUS; ++i) {
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
    }
  }

  // get telemetry
  uint32_t avg_cycles = fpspin_get_avg_cycles(&ctx);
  printf("Handler cycles average: %d\n", avg_cycles);

  fpspin_exit(&ctx);
  return EXIT_SUCCESS;

fail:
  fpspin_exit(&ctx);
  return EXIT_FAILURE;
}
