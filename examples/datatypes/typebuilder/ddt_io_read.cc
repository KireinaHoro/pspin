#include <mpitypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "../handlers/include/datatype_descr.h"

#include "ddt_io_read.h"

#define DDTREBASE(BASE, X) ((uint8_t *) BASE + (uint64_t) X)
// for the NIC, the rebased pointer should fit in a RV32 pointer
#define DDTREBASE_CHECK(BASE, X) ({ \
    assert(!is_remote || (uint64_t)DDTREBASE(BASE, X) <= UINT32_MAX); \
    DDTREBASE(BASE, X); \
})

size_t get_spin_datatype_size(FILE *f){
    spin_datatype_t dt;
    fseek(f, 0, SEEK_SET); //move to the beginning
    size_t bytes = fread(&dt, 1, sizeof(spin_datatype_t), f);
    assert(bytes==sizeof(spin_datatype_t));
    return dt.total_size;
}

void read_spin_datatype(void *mem, size_t size, FILE *f){
    fseek(f, 0, SEEK_SET); //move to the beginning
    size_t bytes = fread(mem, 1, size, f);
    assert(bytes==size);
}

void remap_spin_datatype(void * mem, size_t size, uint64_t base_ptr, bool is_remote){
 
    spin_datatype_t *dt = (spin_datatype_t *) mem;
    
    MPIT_Segment *segp = &(dt->seg);
    for (int32_t i=0; i<=segp->valid_sp; i++){
            
        printf("reading dataloop %u at offset %lx\n", i, (uint64_t) segp->stackelm[i].loop_p);

        //take the real address
        struct MPIT_Dataloop * dataloop = (struct MPIT_Dataloop *) DDTREBASE(mem, segp->stackelm[i].loop_p);

        //substitute with the "virtual" one
        dataloop->el_type = (MPI_Datatype) (uint64_t) DDTREBASE_CHECK(base_ptr, dataloop->el_type);
        segp->stackelm[i].loop_p = (DLOOP_Dataloop *) DDTREBASE_CHECK(base_ptr, segp->stackelm[i].loop_p);
        printf("Remapped loop_p=%p\n", segp->stackelm[i].loop_p);

        switch (dataloop->kind & DLOOP_KIND_MASK)
        {
        case DLOOP_KIND_CONTIG:
	    case DLOOP_KIND_VECTOR:
        {
            //nothing do do
            break;
        }
        case DLOOP_KIND_BLOCKINDEXED:
        {
            dataloop->loop_params.bi_t.offset_array = (DLOOP_Offset *) DDTREBASE_CHECK(base_ptr, dataloop->loop_params.bi_t.offset_array);
            break;
        }
        case DLOOP_KIND_INDEXED:
        {
            dataloop->loop_params.i_t.blocksize_array = (DLOOP_Count *) DDTREBASE_CHECK(base_ptr, dataloop->loop_params.i_t.blocksize_array);
            dataloop->loop_params.i_t.offset_array = (DLOOP_Offset *) DDTREBASE_CHECK(base_ptr, dataloop->loop_params.i_t.offset_array);           
            break;
        }
        case DLOOP_KIND_STRUCT:
        {
            dataloop->loop_params.s_t.blocksize_array = (DLOOP_Count *) DDTREBASE_CHECK(base_ptr, dataloop->loop_params.s_t.blocksize_array);
            dataloop->loop_params.s_t.offset_array = (DLOOP_Offset *) DDTREBASE_CHECK(base_ptr, dataloop->loop_params.s_t.offset_array);

            struct DLOOP_Dataloop **stypes = (struct DLOOP_Dataloop **) DDTREBASE(mem, dataloop->loop_params.s_t.dataloop_array);
            for (int sidx=0; sidx<dataloop->loop_params.s_t.count; sidx++)
            {
                stypes[i] =  (struct DLOOP_Dataloop *) DDTREBASE_CHECK(base_ptr, stypes[i]);
            }

            dataloop->loop_params.s_t.dataloop_array = (struct DLOOP_Dataloop **) DDTREBASE_CHECK(base_ptr, dataloop->loop_params.s_t.dataloop_array);

            break;
        }
        default:
            assert(0);
            break;
        }
    }
}
