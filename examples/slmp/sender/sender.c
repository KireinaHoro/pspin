#include "fpspin/fpspin.h"

#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <assert.h>
#include <bits/types/cookie_io_functions_t.h>
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

#define PAYLOAD_SIZE ((1462 / DMA_ALIGN) * DMA_ALIGN) // 1500 - 20 (IP) - 8 (UDP) - 10 (SLMP)
#define SLMP_PORT 9330

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;

  if (argc != 3) {
    fprintf(stderr, "usage: %s <ip address> <file to transmit>\n", argv[0]);
    return ret;
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

  int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    perror("open socket");
    goto free_buf;
  }
  struct timeval tv = {
      .tv_sec = 0,
      .tv_usec = 100 * 1000, // 100ms
  };
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
    perror("setsockopt");
    goto close_socket;
  }

  struct sockaddr_in server = {
      .sin_family = AF_INET,
      .sin_addr.s_addr = inet_addr(argv[1]),
      .sin_port = htons(SLMP_PORT),
  };

  uint8_t packet[PAYLOAD_SIZE + sizeof(slmp_hdr_t)];
  slmp_hdr_t *hdr = (slmp_hdr_t *)packet;
  uint8_t *payload = packet + sizeof(slmp_hdr_t);

  uint32_t id = rand();
  hdr->msg_id = htonl(id);
  for (uint8_t *cur = file_buf; cur - file_buf < sz; cur += PAYLOAD_SIZE) {
    bool expect_ack = true;
    if (cur + PAYLOAD_SIZE >= file_buf + sz) {
      // last packet requires synchronisation
      hdr->flags = htons(MKEOM | MKSYN);
    } else if (cur == file_buf) {
      // first packet requires synchronisation
      hdr->flags = htons(MKSYN);
    } else {
      hdr->flags = 0;
      expect_ack = false;
    }

    uint32_t offset = cur - file_buf;
    hdr->pkt_off = htonl(offset);

    size_t left = sz - (cur - file_buf);
    size_t to_copy = left > PAYLOAD_SIZE ? PAYLOAD_SIZE : left;

    memcpy(payload, cur, to_copy);

    // send the packet
    if (sendto(sockfd, packet, to_copy + sizeof(slmp_hdr_t), 0,
               (const struct sockaddr *)&server, sizeof(server)) < 0) {
      perror("sendto");
      goto close_socket;
    }

    printf("Sent packet offset=%d in msg #%d\n", offset, id);

    if (expect_ack) {
      uint8_t ack[sizeof(slmp_hdr_t)];
      size_t rcvd = recvfrom(sockfd, ack, sizeof(ack), 0, NULL, NULL);
      // we should be bound at this time == not setting addr
      if (rcvd < 0) {
        perror("recvfrom ACK");
        goto close_socket;
      } else if (rcvd != sizeof(slmp_hdr_t)) {
        fprintf(stderr, "ACK size mismatch");
        goto close_socket;
      }
      slmp_hdr_t *hdr = (slmp_hdr_t *)ack;
      uint16_t flags = ntohs(hdr->flags);
      if (!ACK(flags)) {
        fprintf(stderr, "no ACK set in reply; flag=%#x\n", flags);
        goto close_socket;
      }
    }
  }

  ret = EXIT_SUCCESS;

close_socket:
  close(sockfd);

free_buf:
  free(file_buf);

  return ret;
}
