#include <errno.h>

int *__errno() { return &errno; } 
