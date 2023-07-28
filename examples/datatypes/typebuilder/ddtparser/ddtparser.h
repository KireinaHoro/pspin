#ifndef __DDTPARSER__
#define __DDTPARSER__

#include <mpi.h>

#ifdef __cplusplus
extern "C" {
#endif

MPI_Datatype ddtparser_string2datatype(const char * str);

#ifdef __cplusplus
}
#endif

#endif /* __DDTPARSER__ */
