#ifndef __DDT_IO_READ_H__
#define __DDT_IO_READ_H__

#include <stdio.h>

size_t get_spin_datatype_size(FILE *f);
void read_spin_datatype(void *mem, size_t size, FILE *f);
void remap_spin_datatype(void * mem, size_t size, void *base_ptr);

#endif /* __DDT_IO_READ_H__ */