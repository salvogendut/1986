/*
 * debug.h - minimal VICE debug shim (self-contained).
 *
 * Part of 1986. The VICE-derived core references `debug.maincpu_traceflg`
 * (guarded by DEBUG, which is not defined). This header provides the name so
 * the core builds standalone. GPLv2+; see Development.md.
 */

#ifndef VICE_DEBUG_H
#define VICE_DEBUG_H

struct vice_debug_t {
    int maincpu_traceflg;
};
extern struct vice_debug_t debug;

#endif
