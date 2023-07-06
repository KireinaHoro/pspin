#include <stdio.h>
#include <mpi.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

#include "ddtparser/ddtparser.h"
#include "typebuilder.h"

int main(int argc, char * argv[]){

    MPI_Datatype t;
    int mpi_errno;
    void * buffer;

    if (argc!=4) { 
        printf("Usage: %s <datatype> <count> <out filename>\n", argv[0]);
        exit(1);
    }

    char * dtcompressed = argv[1];
    int count           = atoi(argv[2]);
    char * filename     = argv[3];

    printf("Datatype string : %s\n", dtcompressed);
    printf("Count : %d\n", count);
    
    MPI_Init(&argc, &argv);

    /* build MPI Datatype */
    t = ddtparser_string2datatype(dtcompressed);
 
    typebuilder_convert(t, count, filename);

    MPI_Finalize();

    return 0;
}


