#include "fpspin/fpspin.h"

#include <argp.h>
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <immintrin.h>
#include <netinet/in.h>
#include <omp.h>
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

struct arguments {
  const char *server_addr;
  const char *filename;
  bool force_ack;
  bool parallel;
  int interval;
  int length;
};

static char doc[] = "SLMP file sender\vSend a local file over SLMP.  See the "
                    "thesis for more information.";

static struct argp_option options[] = {
    {"server", 's', "IP", 0, "IP address of the server"},
    {"file", 'f', "FILE", 0, "file to send"},
    {"interval", 'i', "NUM", 0, "per-packet interval to wait for SLMP"},
    {"ack", 'a', 0, 0, "force SLMP ack on every packet"},
    {"parallel", 'p', 0, 0, "parallel send on SLMP socket"},
    {"length", 'l', "NUM", 0, "send <length> bytes instead of the whole file"},
    {0},
};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *args = state->input;

  switch (key) {
  case 's':
    args->server_addr = arg;
    break;
  case 'f':
    args->filename = arg;
    break;
  case 'i':
    args->interval = atoi(arg);
    break;
  case 'a':
    args->force_ack = true;
    break;
  case 'p':
    args->parallel = true;
    break;
  case 'l':
    args->length = atoi(arg);
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static inline double curtime() {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec + now.tv_nsec * 1e-9;
}

int main(int argc, char *argv[]) {
  struct arguments args = {0};
  static struct argp argp = {options, parse_opt, NULL, doc};
  argp_program_version = "slmp-sender 1.0";
  argp_program_bug_address = "Pengcheng Xu <pengxu@ethz.ch>";

  if (argp_parse(&argp, argc, argv, 0, 0, &args)) {
    return -1;
  }
  if (!args.filename) {
    fprintf(stderr, "missing filename (-f)\n");
    return EXIT_FAILURE;
  }
  if (!args.server_addr) {
    fprintf(stderr, "missing server IP address (-s)\n");
    return EXIT_FAILURE;
  }

  int ret = EXIT_FAILURE;

  srand(time(NULL));

  // read file
  FILE *fp;
  if (!(fp = fopen(args.filename, "rb"))) {
    perror("fopen");
    return ret;
  }
  if (!args.length) {
    if (fseek(fp, 0, SEEK_END)) {
      perror("fseek");
      return ret;
    }
    args.length = ftell(fp);
    if (args.length == -1) {
      perror("ftell");
      return ret;
    }
    printf("File size: %d\n", args.length);

    rewind(fp);
  } else {
    fprintf(stderr, "sending the first %d bytes\n", args.length);
  }
  uint8_t *file_buf = malloc(args.length);
  if (!file_buf) {
    perror("malloc");
    return ret;
  }
  if (fread(file_buf, args.length, 1, fp) != 1) {
    // we don't need ferror since we should have the full file
    // XXX: race-condition?
    perror("fread");
    goto free_buf;
  }
  fclose(fp);

  slmp_sock_t sock;
  // TODO: experiment with different aligns for speed
  if (slmp_socket(&sock, args.force_ack, DMA_ALIGN, args.interval,
                  args.parallel)) {
    perror("open socket");
    goto free_buf;
  }
  uint32_t id = rand();
  in_addr_t server = inet_addr(args.server_addr);

  double start = curtime();
  ret = slmp_sendmsg(&sock, server, id, file_buf, args.length);
  double elapsed = curtime() - start;

  if (!ret) {
    printf("Elapsed: %lf\n", elapsed);
  } else {
    printf("Timed out!\n");
  }

  slmp_close(&sock);

free_buf:
  free(file_buf);

  return ret;
}
