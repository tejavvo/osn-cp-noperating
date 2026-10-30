#ifndef CLUSTER_COMMON_PROTOCOL_H
#define CLUSTER_COMMON_PROTOCOL_H

#include "common/messages.h"

/* Full frame = 4-byte body length + version/type/flags/request_id + payload. */
#define CLUSTER_FRAME_HEADER_SIZE 20u
#define CLUSTER_FRAME_MAX_SIZE 65536u
#define CLUSTER_FRAME_MIN_BODY_SIZE 16u

/* Both functions handle exactly one FULL frame, including its length prefix.
 * They do no I/O or allocation. Caller owns all buffers and output objects.
 * encode: on failure, *written=0; capacity shortage returns ERR_TOO_LARGE.
 * decode: frame_size must exactly match the declared length; on failure, the
 * output is zeroed. Reject unsupported version/type/flags, invalid fields,
 * truncated/trailing payload, and oversize values. No partial decoding API.
 * All pointer arguments are required; buffers must not overlap output/input.
 */
cluster_status_t cluster_message_encode(const cluster_message_t *message,
                                        uint8_t *frame, size_t capacity,
                                        size_t *written);
cluster_status_t cluster_message_decode(const uint8_t *frame, size_t frame_size,
                                        cluster_message_t *message);

#endif
