#include "fpspin/fpspin.h"

#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <immintrin.h>
#include <netinet/in.h>
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

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;

  if (argc < 3 || argc > 4) {
    fprintf(stderr, "usage: %s <ip address> <file to transmit> [interval us]\n",
            argv[0]);
    return ret;
  }

  int interval_us = 0;
  if (argc == 4) {
    // basic flow control in sending
    interval_us = atoi(argv[3]);
  }

  srand(time(NULL));

  // read file
  FILE *fp;
  if (!(fp = fopen(argv[2], "rb"))) {
    perror("fopen");
    return ret;
  }
  if (fseek(fp, 0, SEEK_END)) {
    perror("fseek");
    return ret;
  }
  long sz = ftell(fp);
  if (sz == -1) {
    perror("ftell");
    return ret;
  }
  printf("File size: %ld\n", sz);

  rewind(fp);
  uint8_t *file_buf = malloc(sz);
  if (!file_buf) {
    perror("malloc");
    return ret;
  }
  if (fread(file_buf, sz, 1, fp) != 1) {
    // we don't need ferror since we should have the full file
    // XXX: race-condition?
    perror("fread");
    goto free_buf;
  }
  fclose(fp);

  slmp_sock_t sock;
  if (slmp_socket(&sock, false)) {
    perror("open socket");
    goto free_buf;
  }
  uint32_t id = rand();
  in_addr_t server = inet_addr(argv[1]);
  ret = slmp_sendmsg(&sock, server, id, file_buf, sz, interval_us);

  slmp_close(&sock);

free_buf:
  free(file_buf);

  return ret;
}
