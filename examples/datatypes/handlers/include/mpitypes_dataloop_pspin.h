#ifndef _DATALOOP_TYPES_
#define _DATALOOP_TYPES_

// PsPIN-side mpitypes config file for dataloops.

#include <stdint.h>
#include <assert.h>

#define PREPEND_PREFIX(fn) MPIT_ ## fn

// The host side has 64-bit pointers (little endian).
// We take the lower 32-bit word as pointer and ignore the higher (as padding).
#define DECL_PTR(type, name) \
    type name; \
    uint32_t __padding##name;

// need to match MPICH definition on host (used by typebuilder)
// XXX: this is potentially inefficient on PsPIN (64-bit words are not native)
#define MPI_Aint        uint64_t
#define MPI_Datatype    void *

static_assert(sizeof(MPI_Aint) == 8, "MPI_Aint on PsPIN should have the same size as MPICH definition");
static_assert(sizeof(MPI_Datatype) == 4, "MPI_Datatype on PsPIN should have the same size as MPICH definition");

#define MPI_SUCCESS     0

enum {
  MPI_COMBINER_NAMED,
  MPI_COMBINER_DUP,
  MPI_COMBINER_CONTIGUOUS,
  MPI_COMBINER_VECTOR,
  MPI_COMBINER_HVECTOR_INTEGER,
  MPI_COMBINER_HVECTOR,
  MPI_COMBINER_INDEXED,
  MPI_COMBINER_HINDEXED_INTEGER,
  MPI_COMBINER_HINDEXED,
  MPI_COMBINER_INDEXED_BLOCK,
  MPI_COMBINER_STRUCT_INTEGER,
  MPI_COMBINER_STRUCT,
  MPI_COMBINER_SUBARRAY,
  MPI_COMBINER_DARRAY,
  MPI_COMBINER_F90_REAL,
  MPI_COMBINER_F90_COMPLEX,
  MPI_COMBINER_F90_INTEGER,
  MPI_COMBINER_RESIZED,
  MPI_COMBINER_HINDEXED_BLOCK
};

#define DLOOP_Handle        MPI_Datatype
#define DLOOP_Type          MPI_Datatype
#define DLOOP_Offset        MPI_Aint
#define DLOOP_Count         MPI_Aint
#define DLOOP_Buffer        void *
#define DLOOP_VECTOR        struct MPIT_iovec
#define DLOOP_VECTOR_LEN    len
#define DLOOP_VECTOR_BUF    base

struct MPIT_iovec {
    DLOOP_Offset base;
    DLOOP_Offset len;
};

#define ATTRIBUTE(x_) __attribute__(x_)


/* allocate, free, and copy functions must also be defined. */
#define DLOOP_Malloc malloc
#define DLOOP_Free   free
#define DLOOP_Memcpy memcpy

/* debugging output function */
#define DLOOP_dbg_printf printf

/* assert function */
#define DLOOP_Assert assert


/* This is a set of temporary fixes. */
#define DLOOP_OFFSET_CAST_TO_VOID_PTR
#define DLOOP_VOID_PTR_CAST_TO_OFFSET (DLOOP_Offset)(uintptr_t)
#define DLOOP_PTR_DISP_CAST_TO_OFFSET (DLOOP_Offset)(intptr_t)

#define DLOOP_Ensure_Offset_fits_in_pointer(offset_) \
    DLOOP_Assert((offset_) == (DLOOP_Offset)(void *)(offset_))

#define DLOOP_OFFSET_FMT_DEC_SPEC "%lld"
#define DLOOP_OFFSET_FMT_HEX_SPEC "%llx"

/* Marks a variable decl as unused.  Examples:
 *   int UNUSED(foo) = 3;
 *   void bar(int UNUSED(baz)) {} */
#ifndef UNUSED
#define UNUSED(x_) x_##__UNUSED ATTRIBUTE((unused))
#endif

#define DLOOP_Handle_get_size_macro(handle_,size_) \
    MPIT_get_size(handle_,&(size_))

#include "dataloop_parts.h"
#include <assert.h>

void MPIT_get_size(MPI_Datatype type, DLOOP_Offset *size_p);

#endif
