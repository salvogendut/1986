/* lib.h - minimal VICE lib shim (self-contained). GPLv2+; see Development.md. */
#ifndef VICE_LIB_H
#define VICE_LIB_H
#include <stdlib.h>
#include <string.h>
#define lib_malloc(m) malloc(m)
#define lib_calloc(n, m) calloc(n, m)
#define lib_realloc(p, m) realloc(p, m)
#define lib_free(p) free(p)
#define lib_stralloc(s) strdup(s)
#endif
