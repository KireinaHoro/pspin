#ifndef __DATATYPE_DESCR__
#define __DATATYPE_DESCR__

#include <stdint.h>

#define REP

typedef struct type_info{
    DLOOP_Offset size, extent;
    DLOOP_Offset true_lb, true_extent;
} type_info_t;

typedef struct spin_datatype{
    uint32_t total_size; //total size of DDT descr memory (this struct + other memory below it)
    MPIT_Segment seg;
    MPIT_Dataloop dataloop[DLOOP_MAX_DATATYPE_DEPTH];
    struct MPIT_m2m_params params;
    uint32_t count;  
    uint32_t blocks;
    type_info_t info;
    type_info_t subtypes_info_table[DLOOP_MAX_DATATYPE_DEPTH];   
}__attribute__((packed, aligned(32))) spin_datatype_t;

#endif // __DATATYPE_DESCR__
