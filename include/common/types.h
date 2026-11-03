#ifndef CLUSTER_TYPES_H
#define CLUSTER_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#define CLUSTER_MAX_COMMAND_BYTES 4096u
#define CLUSTER_MAX_USERNAME_BYTES 31u
#define CLUSTER_MAX_DIAGNOSTIC_BYTES 1024u
#define CLUSTER_MAX_STRING_BYTES 1024u
#define CLUSTER_MAX_DEPENDENCIES 1024u
#define CLUSTER_MAX_CHECKPOINT_BYTES 60000u
#define CLUSTER_RETAINED_OUTPUT_BYTES 65536u
#define CLUSTER_QUEUE_CAPACITY 1024u
#define CLUSTER_BYTES_PER_MB UINT64_C(1048576)
#define CLUSTER_MAX_ATTEMPTS 3u
#define CLUSTER_FIRST_JOB_ID 1u
#define CLUSTER_FIRST_TEMPLATE_ID 1u
#define CLUSTER_FIRST_WORKER_ID 1001u
#define CLUSTER_UNKNOWN_MEASUREMENT UINT64_MAX
#define CLUSTER_UNKNOWN_CPU_LOAD UINT32_MAX

/* These are host-side records. Encode fields individually for the wire. */
struct resources {
    uint32_t cores;
    uint32_t ram_mb;
    uint32_t gpu;
};

enum job_state {
    JOB_QUEUED = 1,
    JOB_RUNNING = 2,
    JOB_COMPLETED = 3,
    JOB_FAILED = 4,
    JOB_CANCELLED = 5,
    JOB_WAITING = 6,
    JOB_LOST = 7,
    JOB_FAILED_PERMANENT = 8
};

enum worker_state {
    WORKER_ALIVE = 1,
    WORKER_SUSPECTED = 2,
    WORKER_DEAD = 3,
    WORKER_DRAINING = 4
};

enum dependency_kind { DEPENDENCY_JOB = 1, DEPENDENCY_GROUP = 2 };

struct dependency {
    uint8_t kind;
    uint32_t id;
};

struct job_spec {
    char command[CLUSTER_MAX_COMMAND_BYTES + 1u];
    struct resources resources;
    uint32_t time_sec;
    uint8_t priority;
    bool preemptible;
    uint32_t group_id;
    uint32_t every_sec;
    uint32_t dependency_count;
    /* Owner of this record manages the array; it is never sent as a pointer. */
    struct dependency *dependencies;
};

struct telemetry {
    uint64_t ram_bytes;
    uint64_t peak_ram_bytes;
    uint64_t page_faults;
    uint64_t cpu_usec;
};

enum protocol_status {
    STATUS_OK = 0,
    STATUS_INVALID_ARGUMENT = 1,
    STATUS_NOT_FOUND = 2,
    STATUS_QUEUE_FULL = 3,
    STATUS_BAD_STATE = 4,
    STATUS_UNSUPPORTED = 5,
    STATUS_IO_ERROR = 6,
    STATUS_INTERNAL_ERROR = 7,
    STATUS_PERMISSION_DENIED = 8
};

enum finish_reason {
    FINISH_EXITED = 1,
    FINISH_SIGNALLED = 2,
    FINISH_TIMEOUT = 3,
    FINISH_MEMORY_LIMIT = 4,
    FINISH_CANCELLED = 5,
    FINISH_PREEMPTED = 6,
    FINISH_MIGRATED = 7,
    FINISH_START_ERROR = 8
};

enum stop_reason { STOP_CANCEL = 1, STOP_PREEMPT = 2, STOP_MIGRATE = 3 };
enum scheduling_algorithm { SCHEDULING_FIFO = 1, SCHEDULING_PRIORITY = 2 };

#endif
