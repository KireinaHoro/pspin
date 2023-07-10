#ifndef __DDT_IO_READ_H__
#define __DDT_IO_READ_H__

#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t get_spin_datatype_size(FILE *f);
void read_spin_datatype(void *mem, size_t size, FILE *f);

// is_remote: if the target is the NIC.  Performs checks of pointer sizes
void remap_spin_datatype(void * mem, size_t size, uint64_t base_ptr, bool is_remote);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __DDT_IO_READ_H__ */