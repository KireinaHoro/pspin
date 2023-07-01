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
  fpspin_ruleset_udp(&rs);
  if (!fpspin_init(&ctx, PSPIN_DEV, __IMG__, dest_ctx, &rs, 1)) {
    fprintf(stderr, "failed to initialise fpspin\n");
    goto fail;
  }

  // loading finished - application logic from here
  // examples/ping_pong
  while (true) {
    if (exit_flag) {
      printf("\nReceived SIGINT, exiting...\n");
      break;
    }
    for (int i = 0; i < NUM_HPUS; ++i) {
      uint64_t flag_to_host;
      volatile uint8_t *pkt_addr;

      if (!(pkt_addr = fpspin_pop_req(&ctx, i, &flag_to_host)))
        continue;
      volatile pkt_hdr_t *hdrs = (pkt_hdr_t *)pkt_addr;
      volatile uint8_t *payload = (uint8_t *)hdrs + sizeof(pkt_hdr_t);

      // uint16_t dma_len = FLAG_LEN(flag_to_host);
      uint16_t udp_len = ntohs(hdrs->udp_hdr.length);
      uint16_t payload_len = udp_len - sizeof(udp_hdr_t);

      /* printf("Received packet on HPU %d, flag %#lx (id %#lx, dma len %d, UDP "
             "payload len %d):\n",
             i, flag_to_host, FLAG_DMA_ID(flag_to_host), dma_len, payload_len); */

      // to upper
      for (int pi = 0; pi < payload_len; ++pi) {
        volatile char *c = (char *)(payload + pi);
        // FIXME: bounds check for large packets
        volatile char *lower = (char *)(payload + payload_len + pi);
        *lower = *c;
        if (*c == '\n') {
          *c = '|';
        } else {
          *c = toupper(*c);
        }
      }

      // recalculate lengths
      uint16_t ul_host = 2 * payload_len + sizeof(udp_hdr_t);
      uint16_t il_host = sizeof(ip_hdr_t) + ul_host;
      uint16_t return_len = il_host + sizeof(eth_hdr_t);
      hdrs->udp_hdr.length = htons(ul_host);
      hdrs->udp_hdr.checksum = 0;
      hdrs->ip_hdr.length = htons(il_host);
      hdrs->ip_hdr.checksum = 0;
      hdrs->ip_hdr.checksum =
          ip_checksum((uint8_t *)&hdrs->ip_hdr, sizeof(ip_hdr_t));

      // printf("Return packet:\n");
      // hexdump(pkt_addr, return_len);

      uint64_t flag_from_host = MKFLAG(return_len);
      fpspin_push_resp(&ctx, i, flag_from_host);
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
