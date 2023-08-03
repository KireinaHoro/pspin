#include <stdio.h>
#include <mpi.h>
#include <string.h>
#include <assert.h>

extern "C" {
#include <mpitypes.h>
#include "../handlers/include/datatype_descr.h"
#include "typesize_support.h"
}

#include "ddtparser/ddtparser.h"
#include "ddt_io_write.h"
#include "ddt_io_read.h"

#include "typebuilder.h"

void test_open(char * filename){
    FILE *f = fopen(filename, "rb");
    size_t bytes = get_spin_datatype_size(f);
    void * mem = malloc(bytes);

    read_spin_datatype(mem, bytes, f);
    remap_spin_datatype(mem, bytes, (uint64_t) mem, false);

    fclose(f);
    free(mem);
}


int typebuilder_convert(MPI_Datatype t, int count, char *filename){

    MPIT_Segment * segp;
    int mpi_errno;
    void * buffer;

    FILE *outfile = fopen(filename, "wb");


    /* Feed it to MPITypes */
    MPIT_Type_init(t);

 
    MPIT_Type_debug(t);
 
    type_info_t info;
    get_datatype_info(t, &(info));
    buffer = malloc(info.true_extent*count);


    segp = MPIT_Segment_alloc(); // the segment is the state of a dataloop processing
    mpi_errno = MPIT_Segment_init(buffer, count, t, segp, 0);
    if (mpi_errno!=MPI_SUCCESS){ return mpi_errno; } 

    //MPI_Aint blockct;
    //MPIT_Type_blockct(1, t, 0, info.size, &blockct);
    //MPI_Aint *disps = (MPI_Aint *) malloc(sizeof(MPI_Aint) * blockct);
    //int *blocklens = (int *) malloc(sizeof(int) * blockct);
    //MPIT_Offset last = info.size;
    //MPIT_Segment_mpi_flatten(segp,0, &last, blocklens, disps, (int*) &blockct);
    //printf("-------\n");
    //printf("blockct: %li\n", blockct);
    //for (int i=0; i<blockct; i++){
    //    printf("#B %lu %u\n", (long unsigned int) ((uint8_t*) disps[i] - (uint8_t*) buffer), blocklens[i]);
    //}
    //printf("-------\n");

    write_spin_datatype(t, segp, count, outfile);
 
    /* Close&Clean */
    fclose(outfile);
 
    test_open(filename);

    free(buffer);

    return 0;
}

