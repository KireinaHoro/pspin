
#include "mpitypes_dataloop.h"
#include "datatype_descr.h"

#include <stdio.h>

void MPIT_get_size(MPI_Datatype type, DLOOP_Offset *size_p)
{
    /* Now type is actually a pointer to a type_info_t */
    type_info_t * info = (type_info_t *) type;

    *size_p = info->size;
}
