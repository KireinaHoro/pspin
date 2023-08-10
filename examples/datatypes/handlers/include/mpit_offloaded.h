#ifndef MPIT_OFFLOADED
#define MPIT_OFFLOADED

//#include "mpitypes_dataloop.h"
#include <stdint.h>

//##### MPITYPES
// #define DDTCHECK

#ifdef DDTCHECK
#define DDTASSERT(x)                                                           \
  do {                                                                         \
    if (!(x)) {                                                                \
      printf("Assertion failed (%s:%d): " #x "\n", __FILE__, __LINE__);        \
      for (;;)                                                                 \
        ;                                                                      \
    }                                                                          \
  } while (0)
#else
#define DDTASSERT(x)
#endif

static inline void spin_segment_manipulate(struct DLOOP_Segment *segp,
							 DLOOP_Offset first,
							 DLOOP_Offset *lastp,
							 void *pieceparams);

static inline void spin_segment_manipulate_fp(struct DLOOP_Segment *segp,
							 DLOOP_Offset first,
							 DLOOP_Offset *lastp,
							 void *pieceparams);


static inline int ddt_contig(uint32_t myblocks, uint32_t stream_el_size, uint32_t curoffset, struct MPIT_m2m_params *paramp);
static inline int ddt_vector(uint32_t myblocks, uint32_t block_size, uint32_t stride, uint32_t stream_el_size, uint32_t curroffset, struct MPIT_m2m_params *paramp);
static inline int ddt_block_index(uint32_t myblocks, uint32_t block_idx, uint32_t block_size, DLOOP_Offset *offsetarray, uint32_t stream_el_size, uint32_t curroffset, struct MPIT_m2m_params *paramp);
static inline int ddt_index(uint32_t myblocks, uint32_t block_idx, DLOOP_Count *blockarray, DLOOP_Offset *offsetarray, uint32_t stream_el_size, uint32_t curroffset, struct MPIT_m2m_params *paramp);

static inline DLOOP_Count DLOOP_Stackelm_blocksize(struct DLOOP_Dataloop_stackelm *elmp);
static inline DLOOP_Offset DLOOP_Stackelm_offset(struct DLOOP_Dataloop_stackelm *elmp);
static inline void DLOOP_Stackelm_load(struct DLOOP_Dataloop_stackelm *elmp,
									   struct DLOOP_Dataloop *dlp,
									   int branch_flag);

#define DLOOP_SEGMENT_SAVE_LOCAL_VALUES \
	{                                   \
		segp->cur_sp = cur_sp;          \
		segp->valid_sp = valid_sp;      \
		segp->stream_off = stream_off;  \
		*lastp = stream_off;            \
	}

#define DLOOP_SEGMENT_LOAD_LOCAL_VALUES       \
	{                                         \
		last = *lastp;                        \
		cur_sp = segp->cur_sp;                \
		valid_sp = segp->valid_sp;            \
		stream_off = segp->stream_off;        \
		cur_elmp = &(segp->stackelm[cur_sp]); \
	}

#define DLOOP_SEGMENT_RESET_VALUES                                 \
	{                                                              \
		segp->stream_off = 0;                                      \
		segp->cur_sp = 0;                                          \
		cur_elmp = &(segp->stackelm[0]);                           \
		cur_elmp->curcount = cur_elmp->orig_count;                 \
		cur_elmp->orig_block = DLOOP_Stackelm_blocksize(cur_elmp); \
		cur_elmp->curblock = cur_elmp->orig_block;                 \
		cur_elmp->curoffset = cur_elmp->orig_offset +              \
							  DLOOP_Stackelm_offset(cur_elmp);     \
	}

#define DLOOP_SEGMENT_POP_AND_MAYBE_EXIT        \
	{                                           \
		cur_sp--;                               \
		if (cur_sp >= 0)                        \
			cur_elmp = &segp->stackelm[cur_sp]; \
		else                                    \
		{                                       \
			DLOOP_SEGMENT_SAVE_LOCAL_VALUES;    \
			return;                             \
		}                                       \
	}

#define DLOOP_SEGMENT_PUSH                  \
	{                                       \
		cur_sp++;                           \
		DDTASSERT(cur_sp < DLOOP_MAX_DATATYPE_DEPTH); \
		cur_elmp = &segp->stackelm[cur_sp]; \
	}

#define DLOOP_STACKELM_BLOCKINDEXED_OFFSET(elmp_, curcount_) \
	(elmp_)->loop_p->loop_params.bi_t.offset_array[(curcount_)]

#define DLOOP_STACKELM_INDEXED_OFFSET(elmp_, curcount_) \
	(elmp_)->loop_p->loop_params.i_t.offset_array[(curcount_)]

#define DLOOP_STACKELM_INDEXED_BLOCKSIZE(elmp_, curcount_) \
	(elmp_)->loop_p->loop_params.i_t.blocksize_array[(curcount_)]

#define DLOOP_STACKELM_STRUCT_OFFSET(elmp_, curcount_) \
	(elmp_)->loop_p->loop_params.s_t.offset_array[(curcount_)]

#define DLOOP_STACKELM_STRUCT_BLOCKSIZE(elmp_, curcount_) \
	(elmp_)->loop_p->loop_params.s_t.blocksize_array[(curcount_)]

#define DLOOP_STACKELM_STRUCT_EL_EXTENT(elmp_, curcount_) \
	(elmp_)->loop_p->loop_params.s_t.el_extent_array[(curcount_)]

#define DLOOP_STACKELM_STRUCT_DATALOOP(elmp_, curcount_) \
	(elmp_)->loop_p->loop_params.s_t.dataloop_array[(curcount_)]

static inline void spin_segment_manipulate_fp(struct DLOOP_Segment *segp,
							 DLOOP_Offset first,
							 DLOOP_Offset *lastp,
							 void *pieceparams)
{
	enum
	{
		PF_NULL,
		PF_CONTIG,
		PF_VECTOR,
		PF_BLOCKINDEXED,
		PF_INDEXED
	} piecefn_type = PF_NULL;

	/* these four are the "local values": cur_sp, valid_sp, last, stream_off */
	int cur_sp, valid_sp;
	DLOOP_Offset last, stream_off;
	struct DLOOP_Dataloop_stackelm *cur_elmp;

	struct MPIT_m2m_params *paramp = (struct MPIT_m2m_params *)pieceparams;

	DLOOP_SEGMENT_LOAD_LOCAL_VALUES;

	if (first == *lastp)
	{
		/* nothing to do */
		// printf("dloop_segment_manipulate: warning: first == last (" DLOOP_OFFSET_FMT_DEC_SPEC ")\n", first);
		return;
	}

	/* first we ensure that stream_off and first are in the same spot */
	if (first != stream_off)
	{
		if (first < stream_off)
		{
			DLOOP_SEGMENT_RESET_VALUES;
			stream_off = 0;
		}

		if (first != stream_off)
		{
			DLOOP_Offset tmp_last = first;

			/* use manipulate function with a NULL piecefn to advance
	        * stream offset
	        */
			spin_segment_manipulate(segp, stream_off, &tmp_last, NULL);
			/* --BEGIN ERROR HANDLING-- */
			/* verify that we're in the right location */
			DDTASSERT(tmp_last == first);
			/* --END ERROR HANDLING-- */
		}

		DLOOP_SEGMENT_LOAD_LOCAL_VALUES;
	}

#ifdef DDT_TRACE
    if (pieceparams!=NULL) { ptl_epu_trace(); } //do not trace during the catchup
#endif

	for (;;)
	{
		if (cur_elmp->loop_p->kind & DLOOP_FINAL_MASK)
		{
			DLOOP_Offset myblocks, local_el_size, stream_el_size;

			/* structs are never finals (leaves) */
			DDTASSERT((cur_elmp->loop_p->kind & DLOOP_KIND_MASK) !=
					  DLOOP_KIND_STRUCT);

			/* pop immediately on zero count */
			if (cur_elmp->curcount == 0)
				DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;

			/* size on this system of the int, double, etc. that is
	        * the elementary type.
	        */
			local_el_size = cur_elmp->loop_p->el_size;
			stream_el_size = local_el_size;

			/* calculate number of elem. types to work on and function to use.
	        * default is to use the contig piecefn (if there is one).
	        */
			myblocks = cur_elmp->curblock;
			piecefn_type = (paramp != NULL) ? PF_CONTIG : PF_NULL;

			/* check for opportunities to use other piecefns */
			switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
			{
			case DLOOP_KIND_CONTIG:
				break;
			case DLOOP_KIND_VECTOR:
				// only use the vector piecefn if at the start of a
		     	// contiguous block.
		     	//
				if (paramp != NULL && cur_elmp->orig_block == cur_elmp->curblock)
				{
					myblocks = cur_elmp->curblock * cur_elmp->curcount;
					piecefn_type = PF_VECTOR;
				}
				break;
			case DLOOP_KIND_BLOCKINDEXED:
				// only use blkidx piecefn if at start of blkidx type

		     
		    	//printf("DLOOP blockindexed: cur_elmp->orig_block: %u; cur_elmp->curblock : %u; cur_elmp->orig_count: %u; cur_elmp->curcount: %u\n", cur_elmp->orig_block, cur_elmp->curblock, cur_elmp->orig_count, cur_elmp->curcount);
				//fflush(stdout);
				if (paramp != NULL && cur_elmp->orig_block == cur_elmp->curblock)
				{
					// TODO: RELAX CONSTRAINTS
					myblocks = cur_elmp->curblock * cur_elmp->curcount;
					piecefn_type = PF_BLOCKINDEXED;
				}
				break;

			case DLOOP_KIND_INDEXED:
				//only use index piecefn if at start of the index type.
		     	//count test checks that we're on first block.
		     	//block test checks that we haven't made progress on first block.
 		    	//printf("DLOOP indexed: cur_elmp->orig_block: %u; cur_elmp->curblock : %u; cur_elmp->orig_count: %u; cur_elmp->curcount: %u\n", cur_elmp->orig_block, cur_elmp->curblock, cur_elmp->orig_count, cur_elmp->curcount);
				//fflush(stdout);
				if (paramp != NULL && 
					cur_elmp->curblock == DLOOP_STACKELM_INDEXED_BLOCKSIZE(cur_elmp, (cur_elmp->orig_count - cur_elmp->curcount)))
				{
					// TODO: RELAX CONSTRAINT ON COUNT? 
					myblocks = cur_elmp->loop_p->loop_params.i_t.total_blocks;
					piecefn_type = PF_INDEXED;
				}
				break;
			default:
				DDTASSERT(!"unknown dloop kind");
				break;
			}

			/* enforce the last parameter if necessary by reducing myblocks */
			if (last != SEGMENT_IGNORE_LAST && (stream_off + (myblocks * stream_el_size) > last))
			{
				myblocks = ((last - stream_off) / stream_el_size);
				if (myblocks == 0)
				{
					DLOOP_SEGMENT_SAVE_LOCAL_VALUES;
					return;
				}
			}

/*
*/
    

			/* ######### Issuing DMA transfers #########*/
			/* call piecefn to perform data manipulation */
			switch (piecefn_type)
			{
			case PF_NULL:
				break;
			case PF_CONTIG:
                DDTASSERT(myblocks <= cur_elmp->curblock);
				ddt_contig(myblocks, stream_el_size, cur_elmp->curoffset, paramp);
				break;
			case PF_VECTOR:

/*
                if (myblocks > 256){
                    printf("myblocks: %lu; curblock: %lu; curcount: %li; last: %lu; SEGMENT_IGNORE_LAST: %lu; stream_off: %lu; stream_el_size: %lu; cur_sp: %u; valid_sp: %i\n", myblocks, cur_elmp->curblock, cur_elmp->curcount, last, SEGMENT_IGNORE_LAST, stream_off, stream_el_size, cur_sp, segp->valid_sp);
                    fflush(stdout);
                    assert(0);
                }
*/

				ddt_vector(myblocks, cur_elmp->orig_block, cur_elmp->loop_p->loop_params.v_t.stride, stream_el_size, cur_elmp->curoffset, paramp);
				break;
			case PF_BLOCKINDEXED:
				ddt_block_index(myblocks, cur_elmp->orig_count - cur_elmp->curcount, cur_elmp->orig_block, cur_elmp->loop_p->loop_params.bi_t.offset_array, stream_el_size, cur_elmp->curoffset, paramp);
				break;
			case PF_INDEXED:
				ddt_index(myblocks, cur_elmp->orig_count - cur_elmp->curcount, cur_elmp->loop_p->loop_params.i_t.blocksize_array, cur_elmp->loop_p->loop_params.bi_t.offset_array, stream_el_size, cur_elmp->curoffset, paramp);
				break;
			}
			/*######### End DMA issue #########*/

			/* update local values based on piecefn returns (myblocks and
	        * piecefn_indicated_exit)
	        */
			DDTASSERT(myblocks >= 0);
			stream_off += myblocks * stream_el_size;

			/* myblocks of 0 or less than cur_elmp->curblock indicates
	        * that we should stop processing and return.
	        */
			if (myblocks == 0)
			{
				DLOOP_SEGMENT_SAVE_LOCAL_VALUES;
				return;
			}
			else if (myblocks < (DLOOP_Offset)(cur_elmp->curblock))
			{
				cur_elmp->curoffset += myblocks * local_el_size;
				cur_elmp->curblock -= myblocks;

				DLOOP_SEGMENT_SAVE_LOCAL_VALUES;
				return;
			}
			else /* myblocks >= cur_elmp->curblock */
			{
				int count_index = 0;

				/* this assumes we're either *just* processing the last parts
		        * of the current block, or we're processing as many blocks as
		        * we like starting at the beginning of one.
		        */

				switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
				{
				case DLOOP_KIND_INDEXED:
					while (myblocks > 0 && myblocks >= (DLOOP_Offset)(cur_elmp->curblock))
					{
						myblocks -= (DLOOP_Offset)(cur_elmp->curblock);
						cur_elmp->curcount--;
						DDTASSERT(cur_elmp->curcount >= 0);

						count_index = cur_elmp->orig_count - cur_elmp->curcount;
						cur_elmp->curblock = DLOOP_STACKELM_INDEXED_BLOCKSIZE(cur_elmp, count_index);
					}

					if (cur_elmp->curcount == 0)
					{
						/* don't bother to fill in values; we're popping anyway */
						DDTASSERT(myblocks == 0);
						DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
					}
					else
					{
						cur_elmp->orig_block = cur_elmp->curblock;
						cur_elmp->curoffset = cur_elmp->orig_offset + DLOOP_STACKELM_INDEXED_OFFSET(cur_elmp, count_index);

						cur_elmp->curblock -= myblocks;
						cur_elmp->curoffset += myblocks * local_el_size;
					}
					break;
				case DLOOP_KIND_VECTOR:
					/* this math relies on assertions at top of code block */
					cur_elmp->curcount -= myblocks / (DLOOP_Offset)(cur_elmp->curblock);
					if (cur_elmp->curcount == 0)
					{
						DDTASSERT(myblocks % ((DLOOP_Offset)(cur_elmp->curblock)) == 0);
						DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
					}
					else
					{
						/* this math relies on assertions at top of code  block */
						cur_elmp->curblock = cur_elmp->orig_block - (myblocks % (DLOOP_Offset)(cur_elmp->curblock));
						/* new offset = original offset +
			            *              stride * whole blocks +
			            *              leftover bytes
			            */
						cur_elmp->curoffset = cur_elmp->orig_offset +
											  (((DLOOP_Offset)(cur_elmp->orig_count - cur_elmp->curcount)) *
											   cur_elmp->loop_p->loop_params.v_t.stride) +
											  (((DLOOP_Offset)(cur_elmp->orig_block - cur_elmp->curblock)) *
											   local_el_size);
					}
					break;
				case DLOOP_KIND_CONTIG:
					/* contigs that reach this point have always been
			        * completely processed
			        */
					DDTASSERT(myblocks == (DLOOP_Offset)(cur_elmp->curblock) && cur_elmp->curcount == 1);
					DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
					break;
				case DLOOP_KIND_BLOCKINDEXED:
					while (myblocks > 0 && myblocks >= (DLOOP_Offset)(cur_elmp->curblock))
					{
						myblocks -= (DLOOP_Offset)(cur_elmp->curblock);
						cur_elmp->curcount--;
						DDTASSERT(cur_elmp->curcount >= 0);

						count_index = cur_elmp->orig_count - cur_elmp->curcount;
						cur_elmp->curblock = cur_elmp->orig_block;
					}

					if (cur_elmp->curcount == 0)
					{
						/* popping */
						DDTASSERT(myblocks == 0);
						DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
					}
					else
					{
						/* cur_elmp->orig_block = cur_elmp->curblock; */
						cur_elmp->curoffset = cur_elmp->orig_offset + DLOOP_STACKELM_BLOCKINDEXED_OFFSET(cur_elmp, count_index);
						cur_elmp->curblock -= myblocks;
						cur_elmp->curoffset += myblocks * local_el_size;
					}
					break;
				}
			}

		} /* end of if leaf */
		else if (cur_elmp->curblock == 0)
		{

			cur_elmp->curcount--;

			/* new block.  for indexed and struct reset orig_block.
	        * reset curblock for all types
	        */
			switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
			{
			case DLOOP_KIND_CONTIG:
			case DLOOP_KIND_VECTOR:
			case DLOOP_KIND_BLOCKINDEXED:
				break;
			case DLOOP_KIND_INDEXED:
				cur_elmp->orig_block = DLOOP_STACKELM_INDEXED_BLOCKSIZE(cur_elmp, cur_elmp->curcount ? cur_elmp->orig_count - cur_elmp->curcount : 0);
				break;
			case DLOOP_KIND_STRUCT:
				cur_elmp->orig_block = DLOOP_STACKELM_STRUCT_BLOCKSIZE(cur_elmp, cur_elmp->curcount ? cur_elmp->orig_count - cur_elmp->curcount : 0);
				break;
			default:
				/* --BEGIN ERROR HANDLING-- */
				DDTASSERT(!"unknown dloop kind");
				break;
				/* --END ERROR HANDLING-- */
			}
			cur_elmp->curblock = cur_elmp->orig_block;

			if (cur_elmp->curcount == 0)
			{
				DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
			}
		}
		else /* push the stackelm */
		{
			DLOOP_Dataloop_stackelm *next_elmp;
			int count_index, block_index;

			count_index = cur_elmp->orig_count - cur_elmp->curcount;
			block_index = cur_elmp->orig_block - cur_elmp->curblock;

			/* reload the next stackelm if necessary */
			next_elmp = &(segp->stackelm[cur_sp + 1]);
			if (cur_elmp->may_require_reloading)
			{
				DLOOP_Dataloop *load_dlp = NULL;
				switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
				{
				case DLOOP_KIND_CONTIG:
				case DLOOP_KIND_VECTOR:
				case DLOOP_KIND_BLOCKINDEXED:
				case DLOOP_KIND_INDEXED:
					load_dlp = cur_elmp->loop_p->loop_params.cm_t.dataloop;
					break;
				case DLOOP_KIND_STRUCT:
					load_dlp = DLOOP_STACKELM_STRUCT_DATALOOP(cur_elmp,
															  count_index);
					break;
				default:
					/* --BEGIN ERROR HANDLING-- */
					DDTASSERT(!"unknown dloop kind");
					break;
					/* --END ERROR HANDLING-- */
				}

				DLOOP_Stackelm_load(next_elmp, load_dlp, 1);
			}

			/* set orig_offset and all cur values for new stackelm.
	        * this is done in two steps: first set orig_offset based on
	        * current stackelm, then set cur values based on new stackelm.
	        */
			switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
			{
			case DLOOP_KIND_CONTIG:
				next_elmp->orig_offset = cur_elmp->curoffset +
										 (DLOOP_Offset)block_index * cur_elmp->loop_p->el_extent;
				break;
			case DLOOP_KIND_VECTOR:
				/* note: stride is in bytes */
				next_elmp->orig_offset = cur_elmp->orig_offset +
										 (DLOOP_Offset)count_index * cur_elmp->loop_p->loop_params.v_t.stride +
										 (DLOOP_Offset)block_index * cur_elmp->loop_p->el_extent;
				break;
			case DLOOP_KIND_BLOCKINDEXED:
				next_elmp->orig_offset = cur_elmp->orig_offset +
										 (DLOOP_Offset)block_index * cur_elmp->loop_p->el_extent +
										 DLOOP_STACKELM_BLOCKINDEXED_OFFSET(cur_elmp,
																			count_index);
				break;
			case DLOOP_KIND_INDEXED:
				next_elmp->orig_offset = cur_elmp->orig_offset +
										 (DLOOP_Offset)block_index * cur_elmp->loop_p->el_extent +
										 DLOOP_STACKELM_INDEXED_OFFSET(cur_elmp, count_index);
				break;
			case DLOOP_KIND_STRUCT:
				next_elmp->orig_offset = cur_elmp->orig_offset +
										 (DLOOP_Offset)block_index * DLOOP_STACKELM_STRUCT_EL_EXTENT(cur_elmp, count_index) +
										 DLOOP_STACKELM_STRUCT_OFFSET(cur_elmp, count_index);
				break;
			default:
				/* --BEGIN ERROR HANDLING-- */
				DDTASSERT(!"unknown dloop kind");
				break;
				/* --END ERROR HANDLING-- */
			}

			switch (next_elmp->loop_p->kind & DLOOP_KIND_MASK)
			{
			case DLOOP_KIND_CONTIG:
			case DLOOP_KIND_VECTOR:
				next_elmp->curcount = next_elmp->orig_count;
				next_elmp->curblock = next_elmp->orig_block;
				next_elmp->curoffset = next_elmp->orig_offset;
				break;
			case DLOOP_KIND_BLOCKINDEXED:
				next_elmp->curcount = next_elmp->orig_count;
				next_elmp->curblock = next_elmp->orig_block;
				next_elmp->curoffset = next_elmp->orig_offset +
									   DLOOP_STACKELM_BLOCKINDEXED_OFFSET(next_elmp, 0);
				break;
			case DLOOP_KIND_INDEXED:
				next_elmp->curcount = next_elmp->orig_count;
				next_elmp->curblock =
					DLOOP_STACKELM_INDEXED_BLOCKSIZE(next_elmp, 0);
				next_elmp->curoffset = next_elmp->orig_offset +
									   DLOOP_STACKELM_INDEXED_OFFSET(next_elmp, 0);
				break;
			case DLOOP_KIND_STRUCT:
				next_elmp->curcount = next_elmp->orig_count;
				next_elmp->curblock =
					DLOOP_STACKELM_STRUCT_BLOCKSIZE(next_elmp, 0);
				next_elmp->curoffset = next_elmp->orig_offset +
									   DLOOP_STACKELM_STRUCT_OFFSET(next_elmp, 0);
				break;
			default:
				/* --BEGIN ERROR HANDLING-- */
				DDTASSERT(!"unknown dloop kind");
				break;
				/* --END ERROR HANDLING-- */
			}

			cur_elmp->curblock--;
			DLOOP_SEGMENT_PUSH;
#ifdef DDT_TRACE
			ptl_epu_trace();
#endif
		} /* end of else push the stackelm */
	}	 /* end of for (;;) */

	DLOOP_SEGMENT_SAVE_LOCAL_VALUES;
	return;
}

static inline int ddt_contig(uint32_t myblocks, uint32_t stream_el_size, uint32_t curoffset, struct MPIT_m2m_params *paramp)
{
	//printf("ddt contig\n");
	//fflush(stdout);
	uint32_t ctg_size = myblocks * stream_el_size;

	uint32_t dma_source = (uint32_t)paramp->streambuf;
	uint64_t dma_dest = (uint64_t)(paramp->userbuf + curoffset);
	uint32_t dma_size = ctg_size;

/*
    printf("[CONTIG] host_addr: %p\n", paramp->userbuf);
    fflush(stdout);
*/
	// asynchronous DMA; hardware will ensure all commands are finished by the time the handler finishes
	spin_cmd_t dma;
	spin_dma_to_host(dma_dest, dma_source, dma_size, 0, &dma);
	paramp->streambuf += ctg_size;

	return 0;
}

static inline int ddt_vector(uint32_t myblocks, uint32_t block_size, uint32_t stride, uint32_t stream_el_size, uint32_t curroffset, struct MPIT_m2m_params *paramp)
{
	DLOOP_Count i, blocks_left, whole_count;
	uint32_t cbufp = paramp->userbuf + curroffset;

	whole_count = (block_size > 0) ? (myblocks / block_size) : 0;
	blocks_left = (block_size > 0) ? (myblocks % block_size) : 0;

	spin_cmd_t dma;

	for (i = 0; i < whole_count; i++)
	{
        //printf("ddt_vector: copying (1) from %p to %p (size: %u)\n", cbufp, paramp->streambuf, block_size * stream_el_size);
        //fflush(stdout);

		spin_dma_to_host(cbufp, (uint32_t)paramp->streambuf, block_size * stream_el_size, 0, &dma);
		paramp->streambuf += block_size * stream_el_size;
		cbufp += stride;
	}
	if (blocks_left)
	{
		//printf("copying (2) from %p to %p (size: %u)\n", cbufp, paramp->streambuf, blocks_left * stream_el_size);
		//fflush(stdout);

		spin_dma_to_host(cbufp, (uint32_t)paramp->streambuf, blocks_left * stream_el_size, 0, &dma);
		paramp->streambuf += blocks_left * stream_el_size;
	}
	return 0;
}

static inline int ddt_block_index(uint32_t myblocks, uint32_t block_idx, uint32_t block_size, DLOOP_Offset *offsetarray, uint32_t stream_el_size, uint32_t curroffset, struct MPIT_m2m_params *paramp)
{
	//printf("ddt block index\n");
	//fflush(stdout);
	DLOOP_Offset blocks_left = myblocks;
	uint32_t cbufp = paramp->userbuf + curroffset;
	uint32_t dest;
	char const *src;
	DLOOP_Offset const *offsetp = &(offsetarray[block_idx]);
	spin_cmd_t dma;

	int srcsize = stream_el_size * block_size;
	src = paramp->streambuf;

    /*
    printf("[BLOCKIDX] host_addr: %p; offset: %u\n", paramp->userbuf, curroffset);
    fflush(stdout);
    */

	while (blocks_left)
	{
		if (block_size > blocks_left)
		{
			block_size = blocks_left;
			srcsize = stream_el_size * block_size;
		}

		dest = cbufp + *offsetp++;
		spin_dma_to_host(dest, (uint32_t)src, srcsize, 0, &dma);
		src += srcsize;
		blocks_left -= block_size;
	}

	paramp->streambuf = (char *)src;

	return 0;
}

static inline int ddt_index(uint32_t myblocks, uint32_t block_idx, DLOOP_Count *blockarray, DLOOP_Offset *offsetarray, uint32_t stream_el_size, uint32_t curroffset, struct MPIT_m2m_params *paramp)
{
	//printf("ddt index\n");
	//fflush(stdout);
	int curblock = block_idx;
	DLOOP_Offset cur_block_sz, blocks_left = myblocks;
	uint32_t cbufp;
	spin_cmd_t dma;

	while (blocks_left)
	{
		char *src;
		uint32_t dest;

		cur_block_sz = blockarray[curblock];

		cbufp = paramp->userbuf + curroffset + offsetarray[curblock];

		if (cur_block_sz > blocks_left) cur_block_sz = blocks_left;

		src = paramp->streambuf;
		dest = cbufp;
		
		spin_dma_to_host(dest, (uint32_t)src, cur_block_sz * stream_el_size, 0, &dma);

		paramp->streambuf += cur_block_sz * stream_el_size;
		blocks_left -= cur_block_sz;
		curblock++;
	}
	return 0;
}

static inline void spin_segment_manipulate(struct DLOOP_Segment *segp,
							 DLOOP_Offset first,
							 DLOOP_Offset *lastp,
							 void *pieceparams)
{
	/* these four are the "local values": cur_sp, valid_sp, last, stream_off */
	int cur_sp, valid_sp;
	DLOOP_Offset last, stream_off;
	struct DLOOP_Dataloop_stackelm *cur_elmp;

	struct MPIT_m2m_params *paramp = (struct MPIT_m2m_params *)pieceparams;

	DLOOP_SEGMENT_LOAD_LOCAL_VALUES;

	if (first == *lastp)
	{
		/* nothing to do */
		// printf("warning: first == last (" DLOOP_OFFSET_FMT_DEC_SPEC "), segp=%p\n", first, segp);
		return;
	}

	/* first we ensure that stream_off and first are in the same spot */
	if (first != stream_off)
	{
		// printf("segp=%p first=" DLOOP_OFFSET_FMT_DEC_SPEC "; stream_off=" DLOOP_OFFSET_FMT_DEC_SPEC "; resetting.\n", segp, first, stream_off);
		if (first < stream_off)
		{
			DLOOP_SEGMENT_RESET_VALUES;
			stream_off = 0;
		}

		if (first != stream_off)
		{
			DLOOP_Offset tmp_last = first;

			/* use manipulate function with a NULL piecefn to advance
	        * stream offset
	        */
			spin_segment_manipulate(segp, stream_off, &tmp_last, NULL);

			/* --BEGIN ERROR HANDLING-- */
			/* verify that we're in the right location */
			// printf("tmp_last=" DLOOP_OFFSET_FMT_DEC_SPEC ", first=" DLOOP_OFFSET_FMT_DEC_SPEC ", segp=%p\n", tmp_last, first, segp);
			DDTASSERT(tmp_last == first);
			/* --END ERROR HANDLING-- */
		}

		DLOOP_SEGMENT_LOAD_LOCAL_VALUES;

		// printf("done repositioning stream_off; first=" DLOOP_OFFSET_FMT_DEC_SPEC ", stream_off=" DLOOP_OFFSET_FMT_DEC_SPEC ", last=" DLOOP_OFFSET_FMT_DEC_SPEC "\n", first, stream_off, last);
	}

#ifdef DDT_TRACE
    if (pieceparams!=NULL) { ptl_epu_trace(); } //do not trace during the catchup
#endif

	for (;;)
	{
        // asm("// loop start");
		if (cur_elmp->loop_p->kind & DLOOP_FINAL_MASK)
		{
			DLOOP_Offset myblocks, local_el_size, stream_el_size;

			/* structs are never finals (leaves) */
			DDTASSERT((cur_elmp->loop_p->kind & DLOOP_KIND_MASK) !=
				   DLOOP_KIND_STRUCT);

			/* pop immediately on zero count */
			if (cur_elmp->curcount == 0)
				DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;

			/* size on this system of the int, double, etc. that is
	        * the elementary type.
	        */
			local_el_size = cur_elmp->loop_p->el_size;
			stream_el_size = local_el_size;

			/* calculate number of elem. types to work on and function to use.
	        * default is to use the contig piecefn (if there is one).
	        */
			myblocks = cur_elmp->curblock;

			/* enforce the last parameter if necessary by reducing myblocks */
			if (last != SEGMENT_IGNORE_LAST && (stream_off + (myblocks * stream_el_size) > last))
			{
				myblocks = ((last - stream_off) / stream_el_size);
				if (myblocks == 0)
				{
					DLOOP_SEGMENT_SAVE_LOCAL_VALUES;
					return;
				}
			}

			DDTASSERT(myblocks <= cur_elmp->curblock);
			//piecefn_indicated_exit = contigfn(&myblocks,
			//				 el_type,
			//				 cur_elmp->curoffset, /* relative to segp->ptr */
			//				 segp->ptr,			  /* start of buffer (from segment) */
			//				 pieceparams);

			/* ######### Issuing DMA transfer #########*/
			//size_t ctg_size = myblocks * stream_el_size;

			if (paramp!=NULL){
				uint32_t ctg_size = myblocks * stream_el_size;

				//size_t ctg_el_size = ((type_info_t *)el_type)->size;

				//printf("Copying ctg_el_size: %lu; stream_el_size: %lu\n", ctg_size, stream_el_size);
				//fflush(stdout);

				uint32_t dma_source = (uint32_t) paramp->streambuf;
				uint64_t dma_dest = paramp->userbuf + cur_elmp->curoffset;
				uint32_t dma_size = ctg_size;
				spin_cmd_t dma;

				spin_dma_to_host(dma_dest, dma_source, dma_size, 0, &dma);
				paramp->streambuf += ctg_size;
			}
			/*######### End DMA issue #########*/

			/* update local values based on piecefn returns (myblocks and
	        * piecefn_indicated_exit)
	        */
			DDTASSERT(myblocks >= 0);
			stream_off += myblocks * stream_el_size;

			/* myblocks of 0 or less than cur_elmp->curblock indicates
	        * that we should stop processing and return.
	        */
			if (myblocks == 0)
			{
				DLOOP_SEGMENT_SAVE_LOCAL_VALUES;
				return;
			}
			else if (myblocks < (DLOOP_Offset)(cur_elmp->curblock))
			{
				cur_elmp->curoffset += myblocks * local_el_size;
				cur_elmp->curblock -= myblocks;

				DLOOP_SEGMENT_SAVE_LOCAL_VALUES;
				return;
			}
			else /* myblocks >= cur_elmp->curblock */
			{
				int count_index = 0;

				/* this assumes we're either *just* processing the last parts
		        * of the current block, or we're processing as many blocks as
		        * we like starting at the beginning of one.
		        */

				switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
				{
				case DLOOP_KIND_INDEXED:
					while (myblocks > 0 && myblocks >= (DLOOP_Offset)(cur_elmp->curblock))
					{
						myblocks -= (DLOOP_Offset)(cur_elmp->curblock);
						cur_elmp->curcount--;
						DDTASSERT(cur_elmp->curcount >= 0);

						count_index = cur_elmp->orig_count - cur_elmp->curcount;
						cur_elmp->curblock = DLOOP_STACKELM_INDEXED_BLOCKSIZE(cur_elmp, count_index);
					}

					if (cur_elmp->curcount == 0)
					{
						/* don't bother to fill in values; we're popping anyway */
						DDTASSERT(myblocks == 0);
						DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
					}
					else
					{
						cur_elmp->orig_block = cur_elmp->curblock;
						cur_elmp->curoffset = cur_elmp->orig_offset + DLOOP_STACKELM_INDEXED_OFFSET(cur_elmp, count_index);

						cur_elmp->curblock -= myblocks;
						cur_elmp->curoffset += myblocks * local_el_size;
					}
					break;
				case DLOOP_KIND_VECTOR:
					/* this math relies on assertions at top of code block */
					cur_elmp->curcount -= myblocks / (DLOOP_Offset)(cur_elmp->curblock);
					if (cur_elmp->curcount == 0)
					{
						DDTASSERT(myblocks % ((DLOOP_Offset)(cur_elmp->curblock)) == 0);
						DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
					}
					else
					{
						/* this math relies on assertions at top of code  block */
						cur_elmp->curblock = cur_elmp->orig_block - (myblocks % (DLOOP_Offset)(cur_elmp->curblock));
						/* new offset = original offset +
			            *              stride * whole blocks +
			            *              leftover bytes
			            */
						cur_elmp->curoffset = cur_elmp->orig_offset +
											  (((DLOOP_Offset)(cur_elmp->orig_count - cur_elmp->curcount)) *
											   cur_elmp->loop_p->loop_params.v_t.stride) +
											  (((DLOOP_Offset)(cur_elmp->orig_block - cur_elmp->curblock)) *
											   local_el_size);
					}
					break;
				case DLOOP_KIND_CONTIG:
					/* contigs that reach this point have always been
			        * completely processed
			        */
					DDTASSERT(myblocks == (DLOOP_Offset)(cur_elmp->curblock) && cur_elmp->curcount == 1);
					DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
					break;
				case DLOOP_KIND_BLOCKINDEXED:
					while (myblocks > 0 && myblocks >= (DLOOP_Offset)(cur_elmp->curblock))
					{
						myblocks -= (DLOOP_Offset)(cur_elmp->curblock);
						cur_elmp->curcount--;
						DDTASSERT(cur_elmp->curcount >= 0);

						count_index = cur_elmp->orig_count - cur_elmp->curcount;
						cur_elmp->curblock = cur_elmp->orig_block;
					}

					if (cur_elmp->curcount == 0)
					{
						/* popping */
						DDTASSERT(myblocks == 0);
						DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
					}
					else
					{
						/* cur_elmp->orig_block = cur_elmp->curblock; */
						cur_elmp->curoffset = cur_elmp->orig_offset + DLOOP_STACKELM_BLOCKINDEXED_OFFSET(cur_elmp, count_index);
						cur_elmp->curblock -= myblocks;
						cur_elmp->curoffset += myblocks * local_el_size;
					}
					break;
				}
			}


		} /* end of if leaf */
		else if (cur_elmp->curblock == 0)
		{

			cur_elmp->curcount--;

			/* new block.  for indexed and struct reset orig_block.
	        * reset curblock for all types
	        */
			switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
			{
			case DLOOP_KIND_CONTIG:
			case DLOOP_KIND_VECTOR:
			case DLOOP_KIND_BLOCKINDEXED:
				break;
			case DLOOP_KIND_INDEXED:
				cur_elmp->orig_block = DLOOP_STACKELM_INDEXED_BLOCKSIZE(cur_elmp, cur_elmp->curcount ? cur_elmp->orig_count - cur_elmp->curcount : 0);
				break;
			case DLOOP_KIND_STRUCT:
				cur_elmp->orig_block = DLOOP_STACKELM_STRUCT_BLOCKSIZE(cur_elmp, cur_elmp->curcount ? cur_elmp->orig_count - cur_elmp->curcount : 0);
				break;
			default:
				/* --BEGIN ERROR HANDLING-- */
				DDTASSERT(!"unknown dloop kind");
				break;
				/* --END ERROR HANDLING-- */
			}
			cur_elmp->curblock = cur_elmp->orig_block;

			if (cur_elmp->curcount == 0)
			{
				DLOOP_SEGMENT_POP_AND_MAYBE_EXIT;
			}
		}
		else /* push the stackelm */
		{
			DLOOP_Dataloop_stackelm *next_elmp;
			int count_index, block_index;

			count_index = cur_elmp->orig_count - cur_elmp->curcount;
			block_index = cur_elmp->orig_block - cur_elmp->curblock;

			/* reload the next stackelm if necessary */
			next_elmp = &(segp->stackelm[cur_sp + 1]);
			if (cur_elmp->may_require_reloading)
			{
				DLOOP_Dataloop *load_dlp = NULL;
				switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
				{
				case DLOOP_KIND_CONTIG:
				case DLOOP_KIND_VECTOR:
				case DLOOP_KIND_BLOCKINDEXED:
				case DLOOP_KIND_INDEXED:
					load_dlp = cur_elmp->loop_p->loop_params.cm_t.dataloop;
					break;
				case DLOOP_KIND_STRUCT:
					load_dlp = DLOOP_STACKELM_STRUCT_DATALOOP(cur_elmp,
															  count_index);
					break;
				default:
					/* --BEGIN ERROR HANDLING-- */
					DDTASSERT(!"unknown dloop kind");
					break;
					/* --END ERROR HANDLING-- */
				}

				DLOOP_Stackelm_load(next_elmp, load_dlp, 1);
			}

			/* set orig_offset and all cur values for new stackelm.
	        * this is done in two steps: first set orig_offset based on
	        * current stackelm, then set cur values based on new stackelm.
	        */
			switch (cur_elmp->loop_p->kind & DLOOP_KIND_MASK)
			{
			case DLOOP_KIND_CONTIG:
				next_elmp->orig_offset = cur_elmp->curoffset +
										 (DLOOP_Offset)block_index * cur_elmp->loop_p->el_extent;
				break;
			case DLOOP_KIND_VECTOR:
				/* note: stride is in bytes */
				next_elmp->orig_offset = cur_elmp->orig_offset +
										 (DLOOP_Offset)count_index * cur_elmp->loop_p->loop_params.v_t.stride +
										 (DLOOP_Offset)block_index * cur_elmp->loop_p->el_extent;
				break;
			case DLOOP_KIND_BLOCKINDEXED:
				next_elmp->orig_offset = cur_elmp->orig_offset +
										 (DLOOP_Offset)block_index * cur_elmp->loop_p->el_extent +
										 DLOOP_STACKELM_BLOCKINDEXED_OFFSET(cur_elmp,
																			count_index);
				break;
			case DLOOP_KIND_INDEXED:
				next_elmp->orig_offset = cur_elmp->orig_offset +
										 (DLOOP_Offset)block_index * cur_elmp->loop_p->el_extent +
										 DLOOP_STACKELM_INDEXED_OFFSET(cur_elmp, count_index);
				break;
			case DLOOP_KIND_STRUCT:
				next_elmp->orig_offset = cur_elmp->orig_offset +
										 (DLOOP_Offset)block_index * DLOOP_STACKELM_STRUCT_EL_EXTENT(cur_elmp, count_index) +
										 DLOOP_STACKELM_STRUCT_OFFSET(cur_elmp, count_index);
				break;
			default:
				/* --BEGIN ERROR HANDLING-- */
				DDTASSERT(!"unknown dloop kind");
				break;
				/* --END ERROR HANDLING-- */
			}

			switch (next_elmp->loop_p->kind & DLOOP_KIND_MASK)
			{
			case DLOOP_KIND_CONTIG:
			case DLOOP_KIND_VECTOR:
				next_elmp->curcount = next_elmp->orig_count;
				next_elmp->curblock = next_elmp->orig_block;
				next_elmp->curoffset = next_elmp->orig_offset;
				break;
			case DLOOP_KIND_BLOCKINDEXED:
				next_elmp->curcount = next_elmp->orig_count;
				next_elmp->curblock = next_elmp->orig_block;
				next_elmp->curoffset = next_elmp->orig_offset +
									   DLOOP_STACKELM_BLOCKINDEXED_OFFSET(next_elmp, 0);
				break;
			case DLOOP_KIND_INDEXED:
				next_elmp->curcount = next_elmp->orig_count;
				next_elmp->curblock =
					DLOOP_STACKELM_INDEXED_BLOCKSIZE(next_elmp, 0);
				next_elmp->curoffset = next_elmp->orig_offset +
									   DLOOP_STACKELM_INDEXED_OFFSET(next_elmp, 0);
				break;
			case DLOOP_KIND_STRUCT:
				next_elmp->curcount = next_elmp->orig_count;
				next_elmp->curblock =
					DLOOP_STACKELM_STRUCT_BLOCKSIZE(next_elmp, 0);
				next_elmp->curoffset = next_elmp->orig_offset +
									   DLOOP_STACKELM_STRUCT_OFFSET(next_elmp, 0);
				break;
			default:
				/* --BEGIN ERROR HANDLING-- */
				DDTASSERT(!"unknown dloop kind");
				break;
				/* --END ERROR HANDLING-- */
			}

			cur_elmp->curblock--;
			DLOOP_SEGMENT_PUSH;
		} /* end of else push the stackelm */
        // asm("// loop end");
	}	 /* end of for (;;) */

	DLOOP_SEGMENT_SAVE_LOCAL_VALUES;
	return;
}

/* DLOOP_Stackelm_blocksize - returns block size for stackelm based on current
 * count in stackelm.
 *
 * NOTE: loop_p, orig_count, and curcount members of stackelm MUST be correct
 * before this is called!
 *
 */
static inline DLOOP_Count DLOOP_Stackelm_blocksize(struct DLOOP_Dataloop_stackelm *elmp)
{
	struct DLOOP_Dataloop *dlp = elmp->loop_p;

	switch (dlp->kind & DLOOP_KIND_MASK)
	{
	case DLOOP_KIND_CONTIG:
		/* NOTE: we're dropping the count into the
	     * blksize field for contigs, as described
	     * in the init call.
	     */
		return dlp->loop_params.c_t.count;
		break;
	case DLOOP_KIND_VECTOR:
		return dlp->loop_params.v_t.blocksize;
		break;
	case DLOOP_KIND_BLOCKINDEXED:
		return dlp->loop_params.bi_t.blocksize;
		break;
	case DLOOP_KIND_INDEXED:
		return dlp->loop_params.i_t.blocksize_array[elmp->orig_count - elmp->curcount];
		break;
	case DLOOP_KIND_STRUCT:
		return dlp->loop_params.s_t.blocksize_array[elmp->orig_count - elmp->curcount];
		break;
	default:
		/* --BEGIN ERROR HANDLING-- */
		DDTASSERT(!"unknown dloop kind");
		break;
		/* --END ERROR HANDLING-- */
	}
	return -1;
}

/* DLOOP_Stackelm_offset - returns starting offset (displacement) for stackelm
 * based on current count in stackelm.
 *
 * NOTE: loop_p, orig_count, and curcount members of stackelm MUST be correct
 * before this is called!
 *
 * also, this really is only good at init time for vectors and contigs
 * (all the time for indexed) at the moment.
 *
 */
static inline DLOOP_Offset DLOOP_Stackelm_offset(struct DLOOP_Dataloop_stackelm *elmp)
{
	struct DLOOP_Dataloop *dlp = elmp->loop_p;

	switch (dlp->kind & DLOOP_KIND_MASK)
	{
	case DLOOP_KIND_VECTOR:
	case DLOOP_KIND_CONTIG:
		return 0;
		break;
	case DLOOP_KIND_BLOCKINDEXED:
		return dlp->loop_params.bi_t.offset_array[elmp->orig_count - elmp->curcount];
		break;
	case DLOOP_KIND_INDEXED:
		return dlp->loop_params.i_t.offset_array[elmp->orig_count - elmp->curcount];
		break;
	case DLOOP_KIND_STRUCT:
		return dlp->loop_params.s_t.offset_array[elmp->orig_count - elmp->curcount];
		break;
	default:
		/* --BEGIN ERROR HANDLING-- */
		DDTASSERT(!"unknown dloop kind");
		break;
		/* --END ERROR HANDLING-- */
	}
	return -1;
}

/* DLOOP_Stackelm_load
 * loop_p, orig_count, orig_block, and curcount are all filled by us now.
 * the rest are filled in at processing time.
 */
static inline void DLOOP_Stackelm_load(struct DLOOP_Dataloop_stackelm *elmp,
									   struct DLOOP_Dataloop *dlp,
									   int branch_flag)
{
	elmp->loop_p = dlp;
	// printf("Stored loop_p=%p elmp=%p\n", elmp->loop_p, elmp);

	if ((dlp->kind & DLOOP_KIND_MASK) == DLOOP_KIND_CONTIG)
	{
		elmp->orig_count = 1; /* put in blocksize instead */
	}
	else
	{
		elmp->orig_count = dlp->loop_params.count;
	}

	if (branch_flag || (dlp->kind & DLOOP_KIND_MASK) == DLOOP_KIND_STRUCT)
	{
		elmp->may_require_reloading = 1;
	}
	else
	{
		elmp->may_require_reloading = 0;
	}

	/* required by DLOOP_Stackelm_blocksize */
	elmp->curcount = elmp->orig_count;

	elmp->orig_block = DLOOP_Stackelm_blocksize(elmp);
	/* TODO: GO AHEAD AND FILL IN CURBLOCK? */
}


#endif
