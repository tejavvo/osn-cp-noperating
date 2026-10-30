#ifndef CLUSTER_COMMON_TYPES_H
#define CLUSTER_COMMON_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* Project-wide choices. MB means 1024 * 1024 bytes everywhere. */
#define CLUSTER_PROTOCOL_VERSION 1u
#define CLUSTER_USERNAME_MAX 31u
#define CLUSTER_EXECUTABLE_MAX 4095u
#define CLUSTER_DETAIL_MAX 255u
#define CLUSTER_IPV4_TEXT_SIZE 16u
#define CLUSTER_QUEUE_CAPACITY 1024u
#define CLUSTER_DISCOVERY_PORT 9999u
#define CLUSTER_DISCOVERY_ATTEMPTS 3u
#define CLUSTER_DISCOVERY_WAIT_MS 2000u
#define CLUSTER_OUTPUT_INTERVAL_MS 500u
#define CLUSTER_SCHEDULING_INTERVAL_MS 1000u
#define CLUSTER_AGING_INTERVAL_SECONDS 10u
#define CLUSTER_BYTES_PER_MB UINT64_C(1048576)
#define CLUSTER_CHUNK_SIZE 16384u
#define CLUSTER_NODES_PER_REPLY 32u

typedef uint64_t cluster_job_id_t;
typedef uint32_t cluster_worker_id_t;
typedef uint64_t cluster_session_id_t;
typedef uint64_t cluster_request_id_t;

/* Zero is reserved for absent/unassigned IDs. Assigned IDs start at one. */
#define CLUSTER_NO_JOB UINT64_C(0)
#define CLUSTER_NO_WORKER UINT32_C(0)
#define CLUSTER_NO_SESSION UINT64_C(0)
#define CLUSTER_UNSOLICITED UINT64_C(0)

typedef enum {
    CLUSTER_OK = 0,
    CLUSTER_EOF = 1,             /* Clean EOF between frames only. */
    CLUSTER_ERR_IO = -1,
    CLUSTER_ERR_PROTOCOL = -2,
    CLUSTER_ERR_TOO_LARGE = -3,
    CLUSTER_ERR_ARGUMENT = -4,
    CLUSTER_ERR_TIMEOUT = -5,
    CLUSTER_ERR_NO_SCHEDULER = -6
} cluster_status_t;

typedef enum {
    CLUSTER_JOB_QUEUED = 1,
    CLUSTER_JOB_RUNNING = 2,
    CLUSTER_JOB_COMPLETED = 3,
    CLUSTER_JOB_FAILED = 4,
    CLUSTER_JOB_CANCELLED = 5,
    CLUSTER_JOB_WAITING = 6,
    CLUSTER_JOB_LOST = 7,
    CLUSTER_JOB_FAILED_PERMANENT = 8
} cluster_job_state_t;

typedef enum {
    CLUSTER_WORKER_ALIVE = 1,
    CLUSTER_WORKER_SUSPECTED = 2,
    CLUSTER_WORKER_DEAD = 3,
    CLUSTER_WORKER_DRAINING = 4
} cluster_worker_state_t;

typedef enum {
    CLUSTER_SCHEDULE_FIFO = 1,
    CLUSTER_SCHEDULE_PRIORITY = 2
} cluster_scheduling_algorithm_t;

typedef enum {
    CLUSTER_EXEC_PATH = 1,       /* Bare command, resolved by worker's PATH. */
    CLUSTER_EXEC_FILE = 2        /* Scheduler reads and transfers a file. */
} cluster_execution_mode_t;

typedef enum {
    CLUSTER_RESULT_NONE = 0,     /* Not finished yet. */
    CLUSTER_RESULT_EXIT = 1,
    CLUSTER_RESULT_SIGNAL = 2,
    CLUSTER_RESULT_TIMEOUT = 3,
    CLUSTER_RESULT_MEMORY_LIMIT = 4,
    CLUSTER_RESULT_SETUP_ERROR = 5,
    CLUSTER_RESULT_TRANSFER_ERROR = 6
} cluster_result_kind_t;

typedef enum {
    CLUSTER_ERROR_INVALID_REQUEST = 1,
    CLUSTER_ERROR_NOT_LOGGED_IN = 2,
    CLUSTER_ERROR_INVALID_USERNAME = 3,
    CLUSTER_ERROR_QUEUE_FULL = 4,
    CLUSTER_ERROR_JOB_NOT_FOUND = 5,
    CLUSTER_ERROR_EXECUTABLE_UNREADABLE = 6,
    CLUSTER_ERROR_UNSUPPORTED = 7,
    CLUSTER_ERROR_INTERNAL = 8,
    CLUSTER_ERROR_TRANSFER = 9
} cluster_error_code_t;

typedef struct {
    uint32_t cores;
    uint64_t ram_mb;
    uint32_t gpus;
} cluster_resources_t;

typedef struct {
    char executable[CLUSTER_EXECUTABLE_MAX + 1u];
    cluster_execution_mode_t execution_mode;
    cluster_resources_t resources;
    uint32_t time_limit_seconds;
    uint8_t priority;           /* 1 is highest; 3 is lowest. */
} cluster_job_spec_t;

typedef struct {
    cluster_result_kind_t kind;
    uint32_t exit_code;        /* Meaningful only for RESULT_EXIT. */
    uint32_t signal_number;    /* Meaningful only for RESULT_SIGNAL. */
    char detail[CLUSTER_DETAIL_MAX + 1u];
} cluster_job_result_t;

typedef struct {
    cluster_worker_id_t worker_id;
    cluster_worker_state_t state;
    cluster_resources_t total;
    cluster_resources_t free;
    uint32_t running_jobs;
} cluster_node_info_t;

typedef struct {
    cluster_job_id_t job_id;
    cluster_job_state_t state;
    cluster_worker_id_t worker_id;
    char username[CLUSTER_USERNAME_MAX + 1u];
    cluster_job_result_t result;
} cluster_job_info_t;

typedef struct {
    char ipv4[CLUSTER_IPV4_TEXT_SIZE]; /* Numeric dotted IPv4; no DNS. */
    uint16_t port;                     /* Host byte order in memory. */
} cluster_endpoint_t;

/* Implementations belong to src/common/types.c. Error text may be NULL if cap=0.
 * Validations return OK or ERR_ARGUMENT. They do not check file accessibility.
 * All character arrays must be NUL-terminated within their declared capacity.
 */
cluster_status_t cluster_validate_username(const char *username);
cluster_status_t cluster_validate_job_spec(const cluster_job_spec_t *spec,
                                         char *error_text, size_t capacity);
const char *cluster_job_state_name(cluster_job_state_t state);
const char *cluster_worker_state_name(cluster_worker_state_t state);
const char *cluster_error_name(cluster_error_code_t code);

#endif
