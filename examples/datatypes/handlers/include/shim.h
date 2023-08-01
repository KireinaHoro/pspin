#ifndef __SHIM_H__
#define __SHIM_H__

#include "umm_malloc_cfg.h"
#include "umm_malloc.h"

#define uthash_malloc umm_malloc
#define uthash_free(ptr, sz) umm_free(ptr)
#define uthash_fatal(msg)                                                      \
  do {                                                                         \
    printf("FATAL(uthash): " msg);                                             \
  } while (0)

#endif // __SHIM_H__