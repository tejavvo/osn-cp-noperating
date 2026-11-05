#include "common/protocol.h"

#include <errno.h>
#include <stddef.h>

static void encode_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void encode_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static uint16_t decode_u16(const uint8_t *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8) | input[1]);
}

static uint32_t decode_u32(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | input[3];
}

static int valid_header(const struct frame_header *header)
{
    return header->magic == CLUSTER_TCP_MAGIC &&
           header->version == CLUSTER_PROTOCOL_VERSION &&
           header->payload_length <= CLUSTER_MAX_PAYLOAD_BYTES;
}

int protocol_encode_header(const struct frame_header *header,
                           uint8_t output[CLUSTER_HEADER_BYTES])
{
    if (header == NULL || output == NULL || !valid_header(header)) {
        errno = EINVAL;
        return -1;
    }
    encode_u32(output, header->magic);
    encode_u16(output + 4, header->version);
    encode_u16(output + 6, header->type);
    encode_u32(output + 8, header->payload_length);
    encode_u32(output + 12, header->request_id);
    encode_u32(output + 16, header->job_id);
    encode_u32(output + 20, header->run_id);
    return 0;
}

int protocol_decode_header(const uint8_t input[CLUSTER_HEADER_BYTES],
                           struct frame_header *header)
{
    if (input == NULL || header == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct frame_header decoded = {
        .magic = decode_u32(input),
        .version = decode_u16(input + 4),
        .type = decode_u16(input + 6),
        .payload_length = decode_u32(input + 8),
        .request_id = decode_u32(input + 12),
        .job_id = decode_u32(input + 16),
        .run_id = decode_u32(input + 20)
    };
    if (!valid_header(&decoded)) {
        errno = EPROTO;
        return -1;
    }
    *header = decoded;
    return 0;
}
