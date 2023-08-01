#ifndef __UMM_MALLOC_CFGPORT_H__
#define __UMM_MALLOC_CFGPORT_H__

// FIXME: disable after finishing debug
#define UMM_INFO
#define UMM_INTEGRITY_CHECK
#define UMM_POISON_CHECK

#define DBGLOG_LEVEL 4
#include "dbglog.h"

#undef DBGLOG_FUNCTION
#define DBGLOG_FUNCTION(f, ...) printf(__VA_ARGS__)
#define fflush(f)

#endif // __UMM_MALLOC_CFGPORT_H__
