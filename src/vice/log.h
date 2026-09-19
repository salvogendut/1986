/*
 * log.h - minimal VICE log shim (self-contained).
 *
 * Part of 1986. The VICE-derived core references the VICE logging API; this
 * header provides just the names so it builds standalone. Messages are
 * emitted to stderr. GPLv2+; see Development.md.
 */

#ifndef VICE_LOG_H
#define VICE_LOG_H

#include <stdarg.h>
#include <stdio.h>

typedef struct log_s { int unused; } log_t;
#define LOG_DEFAULT NULL

#define log_message(log, ...) do { \
    fprintf(stderr, "[vice] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); \
} while (0)
#define log_error(log, ...)   log_message(log, __VA_ARGS__)
#define log_debug(...)        log_message(NULL, __VA_ARGS__)
#define log_warning(log, ...) log_message(log, __VA_ARGS__)

static inline log_t *log_open(const char *name) { (void)name; return NULL; }

#endif
