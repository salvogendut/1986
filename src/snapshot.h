#pragma once

#include "c128.h"

/* VICE snapshot container (.vsf) support. 1986 writes a private 1986STATE
 * module because x128's own format omits C128 state that 1986 must preserve
 * (notably Z80 and VDC). Foreign VICE machine state is detected and rejected
 * before it can partially mutate the running machine. */
typedef enum {
    SNAPSHOT_OK = 0,
    SNAPSHOT_ERR_ARGUMENT,
    SNAPSHOT_ERR_IO,
    SNAPSHOT_ERR_FORMAT,
    SNAPSHOT_ERR_MACHINE,
    SNAPSHOT_ERR_VERSION,
    SNAPSHOT_ERR_STATE,
    SNAPSHOT_ERR_FOREIGN_STATE
} SnapshotResult;

SnapshotResult snapshot_save(C128 *c128, const char *path);
SnapshotResult snapshot_load(C128 *c128, const char *path);
const char *snapshot_result_name(SnapshotResult result);
