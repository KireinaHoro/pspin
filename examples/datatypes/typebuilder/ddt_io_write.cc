#include <cstdint>
#include <mpitypes.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include "../handlers/include/datatype_descr.h"

#include "ddt_io_write.h"

#define DDTFWRITE(PTR, SIZE, F, OFF) {size_t t = fwrite(PTR, 1, SIZE, F); assert(t==SIZE); OFF += SIZE; assert(ftell(F) == OFF);}

//taken from mpitypes_dataloop.c
void get_datatype_info(MPI_Datatype t, type_info_t *info){
    int mpi_errno;
    
    int size;
    MPI_Aint lb, extent, true_lb, true_extent;

    mpi_errno = PMPI_Type_size(t, &size);
    assert(mpi_errno == MPI_SUCCESS);
	
    mpi_errno = PMPI_Type_get_extent(t, &lb, &extent);
    assert(mpi_errno == MPI_SUCCESS);
	
    mpi_errno = PMPI_Type_get_true_extent(t, &true_lb, &true_extent); 

    info->size        = (DLOOP_Offset) size; 		assert(info->size == size);
    info->extent      = (DLOOP_Offset) extent; 		assert(info->extent == extent);
    info->true_lb     = (DLOOP_Offset) true_lb; 	assert(info->true_lb == true_lb);
    info->true_extent = (DLOOP_Offset) true_extent;	assert(info->true_extent == true_extent);
}

void write_spin_datatype(MPI_Datatype t, MPIT_Segment *segp, int count, FILE *f){
    spin_datatype_t dt;
    fseek(f, 0, SEEK_SET); //move to the beginning

    get_datatype_info(t, &(dt.info));


    dt.params.userbuf = (uint64_t)NULL;
    dt.params.direction = MPIT_MEMCPY_TO_USERBUF;
    dt.count = count;

    // XXX: we assume the entire datatype is smaller than uint32_t (4 GB)
    //      so we can get away with storing offset into 32-bit types
    // assert(sizeof(MPI_Datatype) >= sizeof(type_info_t *)); //just to be safe

    //spin_datatype_t comes first but we still have to fill it
    int ret = fseek(f, sizeof(spin_datatype_t), SEEK_CUR);
    assert(ret==0);

    uint32_t num_blocks = 0;

    printf("sizeof(segment): %lu\n", sizeof(*segp));
    printf("sizeof(stackelm): %lu\n", sizeof(segp->stackelm[0]));
    uint64_t offset = sizeof(spin_datatype_t);
    for (uint32_t i=0; i<=segp->valid_sp; i++){
        //get and write dt element info (needed by the sPIN handlers)
        type_info_t type_info;
        size_t tocopy = sizeof(type_info);
        get_datatype_info(segp->stackelm[i].loop_p->el_type, &type_info);

        // does the datatype get too big?
        assert(offset < UINT32_MAX);
        segp->stackelm[i].loop_p->el_type = (MPI_Datatype) offset; //(MPI_Datatype) &(dt.subtypes_info_table[i]);
        DDTFWRITE(&type_info, tocopy, f, offset);

        struct MPIT_Dataloop * dataloop = segp->stackelm[i].loop_p;
        
        printf("curcount: %li; curoffset: %lu\n", segp->stackelm[i].curcount, segp->stackelm[i].curoffset);


        uint32_t block_this_dataloop = 0;

        switch (dataloop->kind & DLOOP_KIND_MASK)
        {
        case DLOOP_KIND_CONTIG:
            block_this_dataloop = 1;
            break;
	    case DLOOP_KIND_VECTOR:
        {
            block_this_dataloop = dataloop->loop_params.count;
            //nothing do do
            break;
        }
        case DLOOP_KIND_BLOCKINDEXED:
        {
            block_this_dataloop = dataloop->loop_params.count;
            tocopy = sizeof(DLOOP_Offset) * dataloop->loop_params.bi_t.count;
            DLOOP_Offset *offset_array = dataloop->loop_params.bi_t.offset_array;
            dataloop->loop_params.bi_t.offset_array = (DLOOP_Offset *) offset;
            DDTFWRITE(offset_array, tocopy, f, offset);
            break;
        }
        case DLOOP_KIND_INDEXED:
        {
            block_this_dataloop = dataloop->loop_params.count;
            tocopy = sizeof(DLOOP_Count) * dataloop->loop_params.i_t.count;
            DLOOP_Count *blocksize_array = dataloop->loop_params.i_t.blocksize_array;
            dataloop->loop_params.i_t.blocksize_array = (DLOOP_Count *) offset;
            DDTFWRITE(blocksize_array, tocopy, f, offset);

            tocopy = sizeof(DLOOP_Offset) * dataloop->loop_params.i_t.count;
            DLOOP_Offset *offset_array = dataloop->loop_params.i_t.offset_array;
            dataloop->loop_params.i_t.offset_array = (DLOOP_Offset *) offset;
            DDTFWRITE(offset_array, tocopy, f, offset);
            break;
        }
        case DLOOP_KIND_STRUCT:
        {
            block_this_dataloop = dataloop->loop_params.count;
            tocopy = sizeof(DLOOP_Count) * dataloop->loop_params.s_t.count;
            DLOOP_Count *blocksize_array = dataloop->loop_params.s_t.blocksize_array;
            dataloop->loop_params.s_t.blocksize_array = (DLOOP_Count *) offset;
            DDTFWRITE(blocksize_array, tocopy, f, offset);

            tocopy = sizeof(DLOOP_Offset) * dataloop->loop_params.s_t.count;
            DLOOP_Offset *offset_array = dataloop->loop_params.s_t.offset_array;
            dataloop->loop_params.s_t.offset_array = (DLOOP_Offset *) offset;
            DDTFWRITE(offset_array, tocopy, f, offset);

            
            for (int sidx=0; sidx<dataloop->loop_params.s_t.count; sidx++)
            {
                tocopy = sizeof(struct DLOOP_Dataloop);
                struct DLOOP_Dataloop *stype = dataloop->loop_params.s_t.dataloop_array[sidx];
                dataloop->loop_params.s_t.dataloop_array[sidx] = (struct DLOOP_Dataloop *) offset;
                DDTFWRITE(stype, tocopy, f, offset);
            }

            tocopy = sizeof(struct DLOOP_Dataloop*) * dataloop->loop_params.s_t.count;
            struct DLOOP_Dataloop **stypes = dataloop->loop_params.s_t.dataloop_array;
            dataloop->loop_params.s_t.dataloop_array = (struct DLOOP_Dataloop **) offset;
            DDTFWRITE(stypes, tocopy, f, offset);

            break;
        }
        default:
            assert(0);
            break;
        }


        if (num_blocks==0) num_blocks = block_this_dataloop;
        else num_blocks *= block_this_dataloop;

        tocopy = sizeof(struct MPIT_Dataloop);   
        segp->stackelm[i].loop_p = (struct MPIT_Dataloop *) offset;
        printf("writing dataloop %u at offset %lx\n", i, (uint64_t) segp->stackelm[i].loop_p);
        DDTFWRITE(dataloop, tocopy, f, offset);
    }

    /* Copy Segment in dt */
    memcpy(&(dt.seg), segp, sizeof(MPIT_Segment));
    dt.total_size = offset;
    dt.blocks = num_blocks;
    fseek(f, 0, SEEK_SET); //move to the beginning
    fwrite(&dt, 1, sizeof(spin_datatype_t), f);

    /*Print info*/
    printf("Count: %u\n", dt.count);
    printf("Size: %li\n", dt.info.size);
    printf("Extent: %li\n", dt.info.extent);
    printf("True extent: %li\n", dt.info.true_extent);
    printf("True lb: %li\n", dt.info.true_lb);
    printf("Num blocks: %u; avg block len: %f\n", num_blocks, (float) dt.info.size/num_blocks);
}

