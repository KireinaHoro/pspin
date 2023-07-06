#ifndef __TYPEBUILDER_H__
#define __TYPEBUILDER_H__

#include <mpi.h>

int typebuilder_convert(MPI_Datatype t, int count, char *filename);

#endif
