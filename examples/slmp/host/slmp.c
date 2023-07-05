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
  fpspin_ruleset_slmp(&rs);
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

      // no activity from PsPIN expected -- unreachable
      uint64_t flag_from_host = MKFLAG(0);
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
