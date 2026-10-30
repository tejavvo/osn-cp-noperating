#ifndef CLUSTER_COMMON_MESSAGES_H
#define CLUSTER_COMMON_MESSAGES_H

#include "common/types.h"

/* These numeric values are part of protocol v1. Do not reorder/reuse them. */
typedef enum {
    CLUSTER_MSG_LOGIN = 1,
    CLUSTER_MSG_LOGIN_REPLY = 2,
    CLUSTER_MSG_REGISTER = 3,
    CLUSTER_MSG_REGISTER_REPLY = 4,
    CLUSTER_MSG_SUBMIT = 5,
    CLUSTER_MSG_SUBMIT_REPLY = 6,
    CLUSTER_MSG_NODES = 7,
    CLUSTER_MSG_NODES_REPLY = 8,
    CLUSTER_MSG_STATUS = 9,
    CLUSTER_MSG_STATUS_REPLY = 10,
    CLUSTER_MSG_LOGS = 11,
    CLUSTER_MSG_LOGS_REPLY = 12,
    CLUSTER_MSG_ASSIGN = 13,
    CLUSTER_MSG_BINARY_CHUNK = 14,
    CLUSTER_MSG_BINARY_END = 15,
    CLUSTER_MSG_JOB_STARTED = 16,
    CLUSTER_MSG_OUTPUT = 17,
    CLUSTER_MSG_JOB_DONE = 18,
    CLUSTER_MSG_EVENT = 19,
    CLUSTER_MSG_ERROR = 20
} cluster_message_type_t;

/* MORE is legal only for NODES_REPLY and LOGS_REPLY. */
#define CLUSTER_FLAG_MORE UINT32_C(1)

typedef struct {
    char username[CLUSTER_USERNAME_MAX + 1u];
} cluster_login_t;

typedef struct {
    cluster_session_id_t session_id;
    char username[CLUSTER_USERNAME_MAX + 1u];
} cluster_login_reply_t;

typedef struct {
    cluster_resources_t capacity;
} cluster_register_t;

typedef struct {
    cluster_worker_id_t worker_id;
} cluster_register_reply_t;

typedef struct {
    cluster_job_id_t job_id;
    cluster_job_state_t state;
} cluster_submit_reply_t;

typedef struct {
    uint32_t count;
    cluster_node_info_t nodes[CLUSTER_NODES_PER_REPLY];
} cluster_nodes_reply_t;

typedef struct {
    cluster_job_id_t job_id;
} cluster_job_query_t;

typedef struct {
    cluster_job_id_t job_id;
    uint64_t offset;           /* Absolute byte offset, not a chunk number. */
    uint32_t data_length;
    uint8_t data[CLUSTER_CHUNK_SIZE];
} cluster_chunk_t;

typedef struct {
    cluster_job_id_t job_id;
    cluster_worker_id_t worker_id;
    cluster_job_spec_t spec;
    uint64_t binary_size;      /* Zero for PATH; declared file size for FILE. */
} cluster_assignment_t;

typedef struct {
    cluster_job_id_t job_id;
    uint64_t binary_size;
} cluster_binary_end_t;

typedef struct {
    cluster_worker_id_t worker_id;
    cluster_job_id_t job_id;
} cluster_job_started_t;

typedef struct {
    cluster_worker_id_t worker_id;
    cluster_chunk_t chunk;
} cluster_output_t;

typedef struct {
    cluster_worker_id_t worker_id;
    cluster_job_id_t job_id;
    cluster_job_state_t state; /* M1: COMPLETED or FAILED. */
    cluster_job_result_t result;
} cluster_job_done_t;

typedef struct {
    cluster_job_id_t job_id;
    cluster_job_state_t state;
    cluster_worker_id_t worker_id;
    char detail[CLUSTER_DETAIL_MAX + 1u];
} cluster_event_t;

typedef struct {
    cluster_error_code_t code;
    cluster_job_id_t job_id;        /* Zero if no job was assigned/identified. */
    cluster_worker_id_t worker_id;  /* Sender's ID for registered workers; else 0. */
    char detail[CLUSTER_DETAIL_MAX + 1u];
} cluster_error_t;

/* In-memory representation only. Never send sizeof(cluster_message_t) bytes.
 * Zero-initialize before use. The member selected by type is the only payload.
 * NODES requests have no payload. Arrays own their bytes; no pointers to free.
 */
typedef struct {
    cluster_message_type_t type;
    uint32_t flags;
    cluster_request_id_t request_id;
    union {
        cluster_login_t login;
        cluster_login_reply_t login_reply;
        cluster_register_t registration;
        cluster_register_reply_t register_reply;
        cluster_job_spec_t submit;
        cluster_submit_reply_t submit_reply;
        cluster_nodes_reply_t nodes_reply;
        cluster_job_query_t job_query; /* STATUS and LOGS */
        cluster_job_info_t status_reply;
        cluster_chunk_t logs_reply;
        cluster_assignment_t assign;
        cluster_chunk_t binary_chunk;
        cluster_binary_end_t binary_end;
        cluster_job_started_t job_started;
        cluster_output_t output;
        cluster_job_done_t job_done;
        cluster_event_t event;
        cluster_error_t error;
    } payload;
} cluster_message_t;

#endif
