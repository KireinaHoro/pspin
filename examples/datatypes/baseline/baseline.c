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

// MPICH is leaky
const char *__asan_default_options() { return "detect_leaks=0"; }

struct arguments {
  // to avoid printing usage twice
  int rank;

  int num_elements;
  int num_iterations;
  int num_parallel;
  const char *out_file;
  const char *type_descr_str;
  const char *type_idx;
};

static char doc[] =
    "Datatypes baseline program -- send datatypes over MPICH\vSpawn this "
    "baseline on unmodified Corundum to test for default performance of the "
    "types implementation in MPICH.   Adapted from the pingpong demo from ANL "
    "at "
    "https://www.mcs.anl.gov/research/projects/mpi/tutorial/mpiexmpl/src3/"
    "pingpong/C/nbhead/main.html.  Refer to the thesis for more information.";
static char args_doc[] = "TYPE_DESCR_STR TYPE_IDX";

static struct argp_option options[] = {
    {"elements", 'e', "NUM", 0, "number of elements in one datatype message"},
    {"iters", 'i', "NUM", 0, "number of iterations to run for measurements"},
    {"parallel", 'p', "NUM", 0, "number of concurrent send/recvs to run"},
    {"out", 'o', "FILE", 0,
     "CSV file name for writing measurements (only for root rank)"},
    {0}};

static error_t parse_opt(int key, char *arg, struct argp_state *state) {
  struct arguments *args = state->input;

  switch (key) {
  case 'e':
    args->num_elements = atoi(arg);
    break;
  case 'i':
    args->num_iterations = atoi(arg);
    break;
  case 'p':
    args->num_parallel = atoi(arg);
    break;
  case 'o':
    args->out_file = arg;
    break;
  case ARGP_NO_ARGS:
    argp_usage(state);
    break;
  case ARGP_KEY_ARG:
    if (state->arg_num >= 2)
      argp_usage(state);
    else if (state->arg_num == 0)
      args->type_descr_str = arg;
    else
      args->type_idx = arg;
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

typedef struct {
  uint32_t userbuf_size;
  int num_elems;
  uint8_t **userbuf;
} buffers_t;

void free_buffers(buffers_t *bufs) {
  for (int i = 0; i < bufs->num_elems; ++i) {
    free(bufs->userbuf[i]);
  }
  free(bufs->userbuf);
}

buffers_t prepare_buffers(type_info_t *info, struct arguments *args,
                          MPI_Datatype t, int parallels, bool populate) {
  // uint32_t rcvbuff_size = info.true_lb + MAX(MAX(info.extent,
  // info.true_extent), info.size)*count;
  // FIXME: is this ok?

  buffers_t ret = {
      .userbuf_size = info->true_lb +
                      MAX(info->extent, info->true_extent) * args->num_elements,
  };
  ret.userbuf = calloc(parallels, sizeof(uint8_t *));
  ret.num_elems = parallels;
  for (int i = 0; i < parallels; ++i) {
    ret.userbuf[i] = malloc(ret.userbuf_size);
    if (populate) {
      // populate userbuf, only with ASCII-printable bytes
      for (size_t j = 0; j < ret.userbuf_size; ++j) {
        ret.userbuf[i][j] = (uint8_t)(j % (127 - 32) + 32);
      }
    }
  }

  return ret;
}

int main(int argc, char *argv[]) {
  int ret = EXIT_FAILURE;
  srand(time(NULL));

  MPI_Init(&argc, &argv);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  if (world_size != 2) {
    fprintf(stderr, "%s should be launched with exactly 2 ranks; got %d\n",
            argv[0], world_size);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }

  // rank 0:
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  int partner = 1 - world_rank;

  struct arguments args = {
      .num_elements = 1,
      .num_parallel = 1,
      .num_iterations = 20,
  };

  static struct argp argp = {options, parse_opt, args_doc, doc};
  argp_program_version = "datatypes_sender 1.0";
  argp_program_bug_address = "Pengcheng Xu <pengxu@ethz.ch>";

  if (argp_parse(&argp, argc, argv, 0, 0, &args)) {
    exit(EXIT_FAILURE);
  }

  MPI_Request req[args.num_parallel];
  double start, elapsed[args.num_iterations];

  // check arguments
  if (!args.out_file && world_rank == 0) {
    fprintf(stderr, "Warning: not writing performance measurements\n");
  }

  // XXX: we use the type string instead of the binary -- we don't seem to
  //      have a way to deserialise the binary back to a MPI_Datatype
  MPI_Datatype t = ddtparser_string2datatype(args.type_descr_str);

  // allocate userbuf and streambuf
  // buffer size from typetester.cc
  type_info_t info;
  get_datatype_info(t, &(info));
  uint32_t streambuf_size = args.num_elements * info.size;

  // initialise MPITypes
  MPIT_Type_init(t);

  // compile DDT bin to keep in sync -- taken from typebuilder.cc
  MPIT_Type_debug(t);

  MPI_Type_commit(&t);

  FILE *fp = NULL;
  if (args.out_file && world_rank == 0) {
    fp = fopen(args.out_file, "wb");
    if (!fp) {
      perror("fopen output");
      goto mpi_fini;
    }
  }

  buffers_t bufs =
      prepare_buffers(&info, &args, t, args.num_parallel, world_rank != 0);

  for (int i = 0; i < args.num_iterations; ++i) {
    MPI_Sendrecv(MPI_BOTTOM, 0, MPI_INT, partner, 0, MPI_BOTTOM, 0, MPI_INT,
                 partner, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    start = MPI_Wtime();
    for (int j = 0; j < args.num_parallel; ++j) {
      int tag = i * args.num_iterations + j;
      if (world_rank == 0) {
        MPI_Irecv(bufs.userbuf[j], args.num_elements, t, partner, tag,
                  MPI_COMM_WORLD, &req[j]);
      } else {
        MPI_Isend(bufs.userbuf[j], args.num_elements, t, partner, tag,
                  MPI_COMM_WORLD, &req[j]);
      }
    }
    MPI_Waitall(args.num_parallel, req, MPI_STATUSES_IGNORE);
    elapsed[i] = MPI_Wtime() - start;
  }

  if (fp) {
    fprintf(fp, "elements,parallel,streambuf_size,types_idx,types_str\n");
    fprintf(fp, "%d,%d,%d,%s,\"%s\"\n\n", args.num_elements, args.num_parallel,
            streambuf_size, args.type_idx, args.type_descr_str);
    fprintf(fp, "elapsed\n");
    for (int i = 0; i < args.num_iterations; ++i) {
      fprintf(fp, "%lf\n", elapsed[i]);
    }
    fclose(fp);

    printf("Written results to %s\n", args.out_file);
  }

  ret = EXIT_SUCCESS;
  free_buffers(&bufs);

mpi_fini:
  MPI_Finalize();
  return ret;
}
