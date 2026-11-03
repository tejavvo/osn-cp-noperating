#ifndef CLUSTER_PROTOCOL_H
#define CLUSTER_PROTOCOL_H

#include <stdint.h>

#include "common/types.h"

#define CLUSTER_TCP_MAGIC UINT32_C(0x4f534e43)
#define CLUSTER_DISCOVERY_MAGIC UINT32_C(0x4f534e44)
#define CLUSTER_PROTOCOL_VERSION 1u
#define CLUSTER_HEADER_BYTES 24u
#define CLUSTER_MAX_PAYLOAD_BYTES 65536u
#define CLUSTER_MAX_CHUNK_BYTES 60000u
#define CLUSTER_DEFAULT_DISCOVERY_PORT 9999u
#define CLUSTER_DISCOVERY_REQUEST_BYTES 12u
#define CLUSTER_DISCOVERY_REPLY_BYTES 18u
#define CLUSTER_DISCOVERY_ATTEMPTS 3u
#define CLUSTER_DISCOVERY_WAIT_MS 2000u
#define CLUSTER_HEARTBEAT_MS 2000u
#define CLUSTER_OUTPUT_INTERVAL_MS 500u
#define CLUSTER_AGING_INTERVAL_MS 10000u
#define CLUSTER_SHA256_HEX_BYTES 64u

enum protocol_feature {
    FEATURE_CORE = 1,
    FEATURE_RESILIENCE = 2,
    FEATURE_ADVANCED = 4,
    FEATURE_CHECKPOINTS = 8
};

enum discovery_kind { DISCOVERY_REQUEST = 1, DISCOVERY_REPLY = 2 };
enum entity_kind { ENTITY_JOB = 1, ENTITY_CRON = 2 };
enum event_kind { EVENT_JOB_STATE = 1, EVENT_WORKER_STATE = 2, EVENT_CRON_INJECTED = 3 };
enum execution_mode { EXECUTION_PATH = 1, EXECUTION_TRANSFER = 2, EXECUTION_CACHED = 3 };
enum file_kind { FILE_EXECUTABLE = 1, FILE_CHECKPOINT = 2 };

enum message_type {
    MSG_CLIENT_LOGIN = 1,
    MSG_WORKER_REGISTER = 2,
    MSG_WELCOME = 3,
    MSG_END = 4,
    MSG_SUBMIT = 10,
    MSG_SUBMIT_REPLY = 11,
    MSG_NODES = 12,
    MSG_NODE_ITEM = 13,
    MSG_STATUS = 14,
    MSG_STATUS_REPLY = 15,
    MSG_LOGS = 16,
    MSG_LOG_DATA = 17,
    MSG_EVENT = 18,
    MSG_JOB_PREPARE = 20,
    MSG_FILE_BEGIN = 21,
    MSG_FILE_CHUNK = 22,
    MSG_FILE_END = 23,
    MSG_JOB_READY = 24,
    MSG_JOB_START = 25,
    MSG_JOB_STARTED = 26,
    MSG_JOB_OUTPUT = 27,
    MSG_JOB_FINISHED = 28,
    MSG_JOB_REJECTED = 29,
    MSG_HEARTBEAT = 30,
    MSG_JOB_TELEMETRY = 31,
    MSG_CANCEL = 32,
    MSG_LOG_UNFOLLOW = 33,
    MSG_JOB_STOP = 34,
    MSG_WORKER_DRAIN = 35,
    MSG_WORKER_DRAINED = 36,
    MSG_LOG_CAUGHT_UP = 37,
    MSG_CRONS = 40,
    MSG_CRON_ITEM = 41,
    MSG_CANCEL_CRON = 42,
    MSG_CACHE_BEGIN = 43,
    MSG_CACHE_ITEM = 44,
    MSG_CACHE_END = 45,
    MSG_CACHE_MISS = 46,
    MSG_MIGRATE = 50,
    MSG_CHECKPOINT = 51
};

/* Host representation, not a packed wire struct. All fields encode big-endian. */
struct frame_header {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint32_t payload_length;
    uint32_t request_id;
    uint32_t job_id;
    uint32_t run_id;
};

/* Declarations only. Serialization will be implemented in the next step. */
int protocol_encode_header(const struct frame_header *header,
                           uint8_t output[CLUSTER_HEADER_BYTES]);
int protocol_decode_header(const uint8_t input[CLUSTER_HEADER_BYTES],
                           struct frame_header *header);

#endif
