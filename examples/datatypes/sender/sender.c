#include <argp.h>
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
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <mpi.h>

#include "fpspin/fpspin.h"

#include "mpitypes.h"
#include "mpitypes_dataloop.h"

#include "../handlers/include/datatype_descr.h"
#include "../typebuilder/ddt_io_write.h"
#include "../typebuilder/ddtparser/ddtparser.h"

#include "../include/datatypes_host.h"

struct arguments {
  enum {
    MODE_REFERENCE,
    MODE_SENDING,
  } mode;
  int rts_port;
  int slmp_ipg;
  int num_elements;
  const char *out_file;
  const char *ddt_out_file;
  const char *type_descr_str;
};

static char doc[] =
    "Datatypes sender program -- generate and send datatypes elements\vIn the "
    "sending mode, launch the sender to listen for incoming RTS from the host "
    "application.  On receiving a request, the sender will assemble the "
    "datatype as requested and send over SLMP.  In reference mode, we generate "
    "a golden image to compare the userbuf dump from the host application "
    "against.  Refer to the thesis for more information.";
static char args_doc[] = "TYPE_DESCR_STR";

static struct argp_option options[] = {
    {0, 0, 0, 0, "Sending options:"},
    {"rts-port", 'r', "PORT", 0, "UDP port to receive RTS from host app"},
    {"ipg", 'g', "NUM", 0, "inter-packet gap for flow control to SLMP"},

    {0, 0, 0, 0, "Reference options:"},
    {"reference", 'v', 0, 0, "run in reference mode to produce golden image"},
    {"output", 'o', "FILE", 0, "output file of reference golden image"},
    {"descr-output", 'b', "FILE", 0, "output file of compiled DDT image"},
    {"elements", 'e', "NUM", 0, "number of elements in one datatype message"},
    {0}};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *args = state->input;

  switch (key) {
  case 'r':
    args->rts_port = atoi(arg);
    break;
  case 'g':
    args->slmp_ipg = atoi(arg);
    break;
  case 'v':
    args->mode = MODE_REFERENCE;
    break;
  case 'o':
    args->out_file = arg;
    break;
  case 'b':
    args->ddt_out_file = arg;
    break;
  case 'e':
    args->num_elements = atoi(arg);
    break;
  case ARGP_NO_ARGS:
    argp_usage(state);
    break;
  case ARGP_KEY_ARG:
    if (state->arg_num >= 1)
      argp_usage(state);
    args->type_descr_str = arg;
    break;
  case ARGP_KEY_END:
    if (state->arg_num < 1)
      argp_usage(state);
    break;
  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

volatile sig_atomic_t exit_flag = 0;
static void sigint_handler(int signum) { exit_flag = 1; }

typedef struct {
  uint8_t *userbuf, *streambuf;
  uint32_t userbuf_size, streambuf_size;
} buffers_t;

void free_buffers(buffers_t *bufs) {
  free(bufs->streambuf);
  free(bufs->userbuf);
}

buffers_t prepare_buffers(type_info_t *info, struct arguments *args,
                          MPI_Datatype t) {
  // uint32_t rcvbuff_size = info.true_lb + MAX(MAX(info.extent,
  // info.true_extent), info.size)*count;
  // FIXME: is this ok?

  buffers_t ret = {
      .userbuf_size = info->true_lb +
                      MAX(info->extent, info->true_extent) * args->num_elements,
      .streambuf_size = info->size * args->num_elements,
  };
  ret.userbuf = malloc(ret.userbuf_size);
  ret.streambuf = malloc(ret.streambuf_size);

  // populate userbuf, only with ASCII-printable bytes
  for (size_t i = 0; i < ret.userbuf_size; ++i) {
    ret.userbuf[i] = (uint8_t)(i % (127 - 32) + 32);
  }

  // copy from userbuf to streambuf
  // "Start and end refer to starting and ending byte locations in the stream"
  // -- mpitypes.c
  MPIT_Type_memcpy(ret.userbuf, args->num_elements, t, ret.streambuf,
                   MPIT_MEMCPY_FROM_USERBUF, 0, ret.streambuf_size);

  return ret;
}

void update_userbuf_local(buffers_t *bufs, type_info_t *info,
                          struct arguments *args, MPI_Datatype t) {
  // clear out userbuf
  memset(bufs->userbuf, 0, bufs->userbuf_size);

  // copy from streambuf to userbuf
  MPIT_Type_memcpy(bufs->userbuf, args->num_elements, t, bufs->streambuf,
                   MPIT_MEMCPY_TO_USERBUF, 0, bufs->streambuf_size);
}

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;
  srand(time(NULL));

  struct sigaction sa = {
      .sa_handler = sigint_handler,
      .sa_flags = 0,
  };
  sigemptyset(&sa.sa_mask);
  if (sigaction(SIGINT, &sa, NULL)) {
    perror("sigaction");
    goto fail;
  }

  struct arguments args = {
      .mode = MODE_SENDING,
      .rts_port = RTS_PORT,
      .slmp_ipg = 0,
      .num_elements = 1,
  };

  static struct argp argp = {options, parse_opt, args_doc, doc};
  argp_program_version = "datatypes_sender 1.0";
  argp_program_bug_address = "Pengcheng Xu <pengxu@ethz.ch>";

  if (argp_parse(&argp, argc, argv, 0, 0, &args)) {
    exit(EXIT_FAILURE);
  }

  // check arguments
  if (args.mode == MODE_REFERENCE) {
    if (!args.out_file) {
      fprintf(stderr,
              "error: no golden output file specified for reference mode\n");
      exit(EXIT_FAILURE);
    }
    if (!args.ddt_out_file) {
      fprintf(stderr, "error: no type description output file specified for "
                      "reference mode\n");
      exit(EXIT_FAILURE);
    }
  }

  MPI_Init(&argc, &argv);

  // XXX: we use the type string instead of the binary -- we don't seem to
  //      have a way to deserialise the binary back to a MPI_Datatype
  MPI_Datatype t = ddtparser_string2datatype(args.type_descr_str);

  // allocate userbuf and streambuf
  // buffer size from typetester.cc
  type_info_t info;
  get_datatype_info(t, &(info));

  // initialise MPITypes
  MPIT_Type_init(t);

  if (args.mode == MODE_REFERENCE) {
    printf("==> Reference mode\n");
    FILE *fp = fopen(args.out_file, "wb");
    if (!fp) {
      perror("fopen golden");
      goto fail;
    }

    FILE *ddt_fp = fopen(args.ddt_out_file, "wb");
    if (!ddt_fp) {
      perror("fopen ddt bin");
      goto fail;
    }

    // save manipulated userbuf to file
    // XXX: write_spin_datatype corrupts MPI_Datatype so we should do this first
    buffers_t bufs = prepare_buffers(&info, &args, t);
    update_userbuf_local(&bufs, &info, &args, t);

    if (fwrite(bufs.userbuf, bufs.userbuf_size, 1, fp) != 1) {
      perror("fwrite");
      goto mpi_fini;
    }

    printf("Golden result written to %s\n", args.out_file);
    fclose(fp);
    free_buffers(&bufs);

    // compile DDT bin to keep in sync -- taken from typebuilder.cc
    MPIT_Type_debug(t);

    type_info_t info;
    get_datatype_info(t, &(info));
    size_t ddt_len = info.true_extent * args.num_elements;
    void *buffer = malloc(ddt_len);
    memset(buffer, 0, ddt_len);

    MPIT_Segment *segp = MPIT_Segment_alloc(); // the segment is the state of a
                                               // dataloop processing
    int mpi_errno = MPIT_Segment_init(buffer, args.num_elements, t, segp, 0);
    if (mpi_errno != MPI_SUCCESS) {
      fprintf(stderr, "failed to init MPIT segment\n");
      goto fail;
    }
    write_spin_datatype(t, segp, args.num_elements, ddt_fp);
    fclose(ddt_fp);
    free(buffer);

    printf("DDT description binary written to %s\n", args.ddt_out_file);

    ret = EXIT_SUCCESS;
  } else {
    printf("==> Sending mode, waiting for RTS\n");

    // send streambuf in SLMP
    slmp_sock_t sock;
    // datatypes require word alignment
    if (slmp_socket(&sock, 1, 4, args.slmp_ipg, 1)) {
      perror("open socket");
      goto mpi_fini;
    }
    struct sockaddr_in from;
    socklen_t len = sizeof(from);
    datatypes_rts_t rts;

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    ret = EXIT_FAILURE;
    if (sockfd < 0) {
      perror("open rts socket");
      goto mpi_fini;
    }
    struct sockaddr_in server = {
        .sin_family = AF_INET,
        .sin_port = htons(args.rts_port),
    };
    if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0) {
      perror("bind rts socket");
      goto mpi_fini;
    }
    while (true) {
      // receive RTS
      if (recvfrom(sockfd, &rts, sizeof(rts), 0, (struct sockaddr *)&from,
                   &len) < 0) {
        perror("recvfrom");
        goto slmp_close;
      }
      args.num_elements = rts.elem_count;

      char str[40];
      inet_ntop(AF_INET, &from.sin_addr.s_addr, str, sizeof(str));

      printf("... RTS @ %s: %d datatypes, %d parallel msgs\n", str,
             rts.elem_count, rts.num_parallel_msgs);

      buffers_t bufs = prepare_buffers(&info, &args, t);

      // send message in parallel
#pragma omp parallel for
      for (int i = 0; i < rts.num_parallel_msgs; ++i) {
        int msgid;
        if (rts.num_parallel_msgs == 1) {
          // single message -- does not matter
          // multiple parallel messages: use monotonic increasing ID
          msgid = rand();
        } else {
          msgid =
              htonl(i); // small-endian for MPQ -- slmp_sendmsg will swap again
        }

        slmp_sendmsg(&sock, from.sin_addr.s_addr, msgid, bufs.streambuf,
                     bufs.streambuf_size);
      }

      free_buffers(&bufs);

      if (exit_flag) {
        fprintf(stderr, "Received SIGINT, exiting...\n");
        break;
      }
    }

    ret = EXIT_SUCCESS;

  slmp_close:
    slmp_close(&sock);
  }

mpi_fini:
  MPI_Finalize();

fail:
  return ret;
}
