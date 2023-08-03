#include <stdio.h>
#include <mpi.h>
#include <string.h>
#include <assert.h>

#include <liblsb.h>

extern "C" {
#include <mpitypes.h>
#include "../handlers/include/datatype_descr.h"
#include "typesize_support.h"
}

#include "ddtparser/ddtparser.h"
#include "ddt_io_write.h"
#include "ddt_io_read.h"

#define MAX(a, b) ((a > b) ? (a) : (b))

#define FLUSH_ARRAY_SIZE 50*1024*1024

static inline void flush_cache(){
    static volatile int flush_array[FLUSH_ARRAY_SIZE];

    volatile uint64_t dummysum=0;
    for (uint32_t i=0; i<FLUSH_ARRAY_SIZE; i++){
        flush_array[i] = rand();
        dummysum += flush_array[i];
    } 
    printf("dummysum: %lu (%u)\n", dummysum, FLUSH_ARRAY_SIZE);
}

int main(int argc, char * argv[]){

    FILE * outfile;
    MPI_Datatype t;
    MPIT_Segment * segp;
    struct MPIT_m2m_params params;
    int mpi_errno;
    void * sndbuff;
    void * rcvbuff;

    if (argc!=3) { 
        printf("Usage: %s <datatype> <count>\n", argv[0]);
        exit(1);
    }

    char * dtcompressed = argv[1];
    int count           = atoi(argv[2]);

    printf("Datatype string : %s\n", dtcompressed);
    printf("Count : %d\n", count);

    MPI_Init(&argc, &argv);
    LSB_Init("test", 0);    

    /* build MPI Datatype */
    t = ddtparser_string2datatype(dtcompressed);
 

 
    //MPIT_Type_debug(t);
 
    type_info_t info;
    get_datatype_info(t, &(info));


    // uint32_t rcvbuff_size = info.true_lb + MAX(MAX(info.extent, info.true_extent), info.size)*count;
    // FIXME: is this ok?
    uint32_t rcvbuff_size = info.true_lb + MAX(info.extent, info.true_extent) * count;
    uint32_t sndbuff_size = info.size;

    rcvbuff = malloc(rcvbuff_size);
    sndbuff = malloc(sndbuff_size);

    LSB_Set_Rparam_string("ddt", dtcompressed);
    LSB_Set_Rparam_int("sndbuff", sndbuff_size);
    LSB_Set_Rparam_int("rcvbuff", rcvbuff_size);
    LSB_Set_Rparam_int("ddt_size", info.size);
    LSB_Set_Rparam_int("ddt_extent", info.extent);
    LSB_Set_Rparam_int("ddt_true_extent", info.true_extent);
    LSB_Set_Rparam_int("ddt_true_lb", info.true_lb);

    if (mpi_errno!=MPI_SUCCESS){ return mpi_errno; } 

    for (int i=0; i<10; i++){       

        flush_cache();

        /* Feed it to MPITypes */
        MPIT_Type_init(t);


        LSB_Res();
        MPIT_Type_memcpy(rcvbuff, 1, t, sndbuff, MPIT_MEMCPY_TO_USERBUF, 0, info.size);
        LSB_Rec(i);
    }
 
    LSB_Finalize();
    free(rcvbuff);
    free(sndbuff);
    MPI_Finalize();

    return 0;
}


