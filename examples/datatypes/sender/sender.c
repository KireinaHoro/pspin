#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <immintrin.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <mpi.h>

#include "fpspin/fpspin.h"

#include "mpitypes.h"
#include "mpitypes_dataloop.h"

#include "../handlers/datatype_descr.h"
#include "../typebuilder/ddt_io_write.h"
#include "../typebuilder/ddtparser/ddtparser.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))

bool is_golden;

_Noreturn void usage(char *argv0) {
  fprintf(stderr,
          "usage: %s <type string> <count> <test|golden> "
          "<server|golden filename>\n",
          argv0);
  exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;
  FILE *fp = NULL;
  in_addr_t server = {0};
  srand(time(NULL));

  if (argc != 5) {
    usage(argv[0]);
  }
  if (!strcmp(argv[3], "test")) {
    is_golden = false;
    server = inet_addr(argv[4]);
  } else if (!strcmp(argv[3], "golden")) {
    is_golden = true;
  } else {
    usage(argv[0]);
  }

  if (is_golden) {
    fp = fopen(argv[4], "wb");
    if (!fp) {
      perror("fopen golden");
      goto fail;
    }
  }

  int count = atoi(argv[2]);

  MPI_Init(&argc, &argv);

  // XXX: we use the type string instead of the binary -- we don't seem to
  //      have a way to deserialise the binary back to a MPI_Datatype
  MPI_Datatype t = ddtparser_string2datatype(argv[1]);

  // allocate userbuf and streambuf
  // buffer size from typetester.cc
  type_info_t info;
  get_datatype_info(t, &(info));

  // initialise MPITypes
  MPIT_Type_init(t);

  // uint32_t rcvbuff_size = info.true_lb + MAX(MAX(info.extent,
  // info.true_extent), info.size)*count;
  // FIXME: is this ok?
  uint32_t userbuf_size =
      info.true_lb + MAX(info.extent, info.true_extent) * count;
  uint32_t streambuf_size = info.size;

  uint8_t *userbuf = malloc(userbuf_size);
  uint8_t *streambuf = malloc(streambuf_size);

  // populate userbuf, only with ASCII-printable bytes
  for (size_t i = 0; i < userbuf_size; ++i) {
    userbuf[i] = (uint8_t)(i % (127 - 32) + 32);
  }

  // copy from userbuf to streambuf
  // "Start and end refer to starting and ending byte locations in the stream"
  // -- mpitypes.c
  MPIT_Type_memcpy(userbuf, 1, t, streambuf, MPIT_MEMCPY_FROM_USERBUF, 0,
                   info.size);

  // clear out userbuf
  memset(userbuf, 0, userbuf_size);

  if (is_golden) {
    // copy from streambuf to userbuf
    MPIT_Type_memcpy(userbuf, 1, t, streambuf, MPIT_MEMCPY_TO_USERBUF, 0,
                     info.size);

    // save manipulated userbuf to file
    if (fwrite(userbuf, userbuf_size, 1, fp) != 1) {
      perror("fwrite");
      goto mpi_fini;
    }

    printf("Golden result written to %s\n", argv[4]);
    ret = EXIT_SUCCESS;
  } else {
    // send streambuf in SLMP
    int sockfd = slmp_socket();
    if (sockfd < 0) {
      perror("open socket");
      goto mpi_fini;
    }

    // no flow control for now
    ret = slmp_sendmsg(sockfd, server, rand(), streambuf, streambuf_size, 0);

    close(sockfd);
  }

mpi_fini:
  MPI_Finalize();

  if (fp)
    fclose(fp);

fail:
  return ret;
}
