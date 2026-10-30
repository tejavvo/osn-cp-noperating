#ifndef CLUSTER_COMMON_LOG_H
#define CLUSTER_COMMON_LOG_H

#include "common/types.h"

typedef enum {
    CLUSTER_LOG_SCHEDULER = 1,
    CLUSTER_LOG_WORKER = 2,
    CLUSTER_LOG_CLIENT = 3
} cluster_log_component_t;

/* One thread-safe stdout line, flushed before return, using local 24-hour time:
 * [HH:MM:SS] [SCHEDULER] text
 * [HH:MM:SS] [WORKER] text           when worker_id == 0
 * [HH:MM:SS] [WORKER 1001] text      after registration
 * [HH:MM:SS] [CLIENT] text
 * Ignore worker_id for other components. Callers omit the trailing newline.
 * Formatter adds one; embedded CR/LF are escaped so one call is one log line.
 * Do not call from a signal handler or between fork and exec in a threaded
 * process. Report child setup errors through a pipe to its parent instead.
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
void log_line(cluster_log_component_t component, cluster_worker_id_t worker_id,
              const char *format, ...);

#endif
