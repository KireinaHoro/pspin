#ifndef __DDT_IO_WRITE_H__
#define __DDT_IO_WRITE_H__

#include <stdio.h>
#include <mpitypes.h>
#include "../handlers/datatype_descr.h"

void get_datatype_info(MPI_Datatype t, type_info_t *info);
void write_spin_datatype(MPI_Datatype t, MPIT_Segment *segp, int count, FILE *f);


#endif /* __DDT_IO_WRITE_H__ */