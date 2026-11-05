#include "common/connection.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s (errno=%d)\n", \
                __FILE__, __LINE__, #condition, errno); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

struct captured {
    unsigned count;
    struct frame_header headers[8];
    uint8_t payloads[8][256];
};

static struct frame_header make_header(uint16_t type, uint32_t length, uint32_t request)
{
    struct frame_header header = {
        .magic = CLUSTER_TCP_MAGIC,
        .version = CLUSTER_PROTOCOL_VERSION,
        .type = type,
        .payload_length = length,
        .request_id = request,
        .job_id = 0,
        .run_id = 0
    };
    return header;
}

static int capture(struct connection *connection, const struct frame_header *header,
                   const uint8_t *payload, void *context)
{
    (void)connection;
    struct captured *captured = context;
    CHECK(captured->count < 8);
    CHECK(header->payload_length <= sizeof(captured->payloads[0]));
    captured->headers[captured->count] = *header;
    if (header->payload_length != 0) {
        CHECK(payload != NULL);
        memcpy(captured->payloads[captured->count], payload, header->payload_length);
    } else {
        CHECK(payload == NULL);
    }
    ++captured->count;
    return 0;
}

static void send_small(int fd, const uint8_t *data, size_t length)
{
    /* Used only for small raw fragments that fit in an empty socket buffer. */
    size_t sent = 0;
    while (sent < length) {
        ssize_t count = send(fd, data + sent, length - sent, 0);
        CHECK(count > 0);
        sent += (size_t)count;
    }
}

static void test_header(void)
{
    const uint8_t golden[24] = {
        0x4f, 0x53, 0x4e, 0x43, 0x00, 0x01, 0x00, 0x0c,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    uint8_t encoded[24];
    struct frame_header header = make_header(MSG_NODES, 0, 1);
    CHECK(protocol_encode_header(&header, encoded) == 0);
    CHECK(memcmp(encoded, golden, sizeof(golden)) == 0);
    struct frame_header decoded = {0};
    CHECK(protocol_decode_header(golden, &decoded) == 0);
    CHECK(decoded.magic == header.magic && decoded.version == header.version);
    CHECK(decoded.type == MSG_NODES && decoded.payload_length == 0);
    CHECK(decoded.request_id == 1 && decoded.job_id == 0 && decoded.run_id == 0);

    header = make_header(MSG_JOB_OUTPUT, CLUSTER_MAX_PAYLOAD_BYTES, UINT32_MAX);
    header.job_id = UINT32_C(0x12345678);
    header.run_id = UINT32_C(0xabcdef01);
    CHECK(protocol_encode_header(&header, encoded) == 0);
    CHECK(encoded[8] == 0 && encoded[9] == 1 && encoded[10] == 0 && encoded[11] == 0);
    CHECK(encoded[16] == 0x12 && encoded[19] == 0x78);
    CHECK(encoded[20] == 0xab && encoded[23] == 1);
    CHECK(protocol_decode_header(encoded, &decoded) == 0);
    CHECK(decoded.payload_length == CLUSTER_MAX_PAYLOAD_BYTES);
    CHECK(decoded.request_id == UINT32_MAX && decoded.job_id == header.job_id);
    CHECK(decoded.run_id == header.run_id);

    for (unsigned invalid = 0; invalid < 3; ++invalid) {
        struct frame_header bad = header;
        if (invalid == 0) bad.magic = 0;
        if (invalid == 1) bad.version = 2;
        if (invalid == 2) bad.payload_length = CLUSTER_MAX_PAYLOAD_BYTES + 1;
        memset(encoded, 0xaa, sizeof(encoded));
        CHECK(protocol_encode_header(&bad, encoded) == -1);
        for (size_t i = 0; i < sizeof(encoded); ++i) CHECK(encoded[i] == 0xaa);
        CHECK(protocol_encode_header(&header, encoded) == 0);
        if (invalid == 0) encoded[0] = 0;
        if (invalid == 1) encoded[5] = 2;
        if (invalid == 2) encoded[8] = 0xff;
        decoded = make_header(MSG_STATUS, 0, 99);
        CHECK(protocol_decode_header(encoded, &decoded) == -1 && errno == EPROTO);
        CHECK(decoded.type == MSG_STATUS && decoded.request_id == 99);
    }
    CHECK(protocol_encode_header(NULL, encoded) == -1);
    CHECK(protocol_encode_header(&header, NULL) == -1);
    CHECK(protocol_decode_header(NULL, &decoded) == -1);
    CHECK(protocol_decode_header(encoded, NULL) == -1);
    puts("PASS header encoding, fixed wire example, boundaries, and invalid input");
}

static void test_fragmented_and_combined(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct captured captured = {0};
    struct connection *connection = connection_create(sockets[0], capture, &captured);
    CHECK(connection != NULL);
    CHECK((fcntl(connection_fd(connection), F_GETFL, 0) & O_NONBLOCK) != 0);
    CHECK(connection_receive(connection) == CONNECTION_AGAIN);

    uint8_t header_bytes[24];
    const uint8_t binary[] = {0, 0xff, '\n', 0, 'x'};
    struct frame_header header = make_header(MSG_LOG_DATA, sizeof(binary), 7);
    CHECK(protocol_encode_header(&header, header_bytes) == 0);
    for (size_t i = 0; i < sizeof(header_bytes); ++i) {
        send_small(sockets[1], header_bytes + i, 1);
        CHECK(connection_receive(connection) == CONNECTION_PROGRESS);
        CHECK(captured.count == 0);
        CHECK(connection_receive(connection) == CONNECTION_AGAIN);
    }
    for (size_t i = 0; i < sizeof(binary); ++i) {
        send_small(sockets[1], binary + i, 1);
        CHECK(connection_receive(connection) == CONNECTION_PROGRESS);
        CHECK(captured.count == (i + 1 == sizeof(binary) ? 1u : 0u));
    }
    CHECK(captured.headers[0].request_id == 7);
    CHECK(memcmp(captured.payloads[0], binary, sizeof(binary)) == 0);

    uint8_t combined[24 + 24 + sizeof(binary)];
    header = make_header(MSG_NODES, 0, 8);
    CHECK(protocol_encode_header(&header, combined) == 0);
    header = make_header(MSG_LOG_DATA, sizeof(binary), 9);
    CHECK(protocol_encode_header(&header, combined + 24) == 0);
    memcpy(combined + 48, binary, sizeof(binary));
    send_small(sockets[1], combined, sizeof(combined));
    CHECK(connection_receive(connection) == CONNECTION_PROGRESS);
    CHECK(captured.count == 3);
    CHECK(captured.headers[1].type == MSG_NODES && captured.headers[1].request_id == 8);
    CHECK(captured.headers[2].request_id == 9);
    CHECK(memcmp(captured.payloads[2], binary, sizeof(binary)) == 0);
    close(sockets[1]);
    CHECK(connection_receive(connection) == CONNECTION_EOF);
    CHECK(connection_receive(connection) == CONNECTION_EOF);
    connection_destroy(connection);
    puts("PASS fragmented headers/payloads, binary data, combined and empty frames");
}

static void test_bad_frames(void)
{
    for (unsigned kind = 0; kind < 5; ++kind) {
        int sockets[2];
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        struct captured captured = {0};
        struct connection *connection = connection_create(sockets[0], capture, &captured);
        CHECK(connection != NULL);
        uint8_t bytes[26] = {0};
        struct frame_header header = make_header(MSG_LOG_DATA, 4, 1);
        CHECK(protocol_encode_header(&header, bytes) == 0);
        size_t length = 24;
        if (kind == 0) bytes[0] = 0;             /* Bad magic. */
        if (kind == 1) bytes[5] = 2;             /* Bad version. */
        if (kind == 2) bytes[8] = 0xff;          /* Oversized payload. */
        if (kind == 3) length = 23;              /* Truncated header. */
        if (kind == 4) length = sizeof(bytes);   /* Truncated payload. */
        send_small(sockets[1], bytes, length);
        CHECK(shutdown(sockets[1], SHUT_WR) == 0);
        errno = 0;
        CHECK(connection_receive(connection) == CONNECTION_FATAL);
        CHECK(errno == EPROTO);
        CHECK(connection_fd(connection) == -1 && captured.count == 0);
        CHECK(connection_receive(connection) == CONNECTION_FATAL && errno == EBADF);
        connection_destroy(connection);
        close(sockets[1]);
    }
    puts("PASS malformed headers, oversized frames, and EOF inside a frame");
}

static int check_large(struct connection *connection, const struct frame_header *header,
                       const uint8_t *payload, void *context)
{
    (void)connection;
    unsigned *received = context;
    CHECK(header->request_id == *received + 1);
    CHECK(header->payload_length == CLUSTER_MAX_PAYLOAD_BYTES);
    for (size_t i = 0; i < header->payload_length; ++i) {
        CHECK(payload[i] == (uint8_t)(i + header->request_id));
    }
    ++*received;
    return 0;
}

static void test_backpressure(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    int small_buffer = 1024;
    CHECK(setsockopt(sockets[0], SOL_SOCKET, SO_SNDBUF,
                     &small_buffer, sizeof(small_buffer)) == 0);
    struct captured unused = {0};
    unsigned received = 0;
    struct connection *sender = connection_create(sockets[0], capture, &unused);
    struct connection *receiver = connection_create(sockets[1], check_large, &received);
    CHECK(sender != NULL && receiver != NULL);
    uint8_t *payload = malloc(CLUSTER_MAX_PAYLOAD_BYTES);
    CHECK(payload != NULL);
    for (unsigned id = 1; id <= 8; ++id) {
        for (size_t i = 0; i < CLUSTER_MAX_PAYLOAD_BYTES; ++i) payload[i] = (uint8_t)(i + id);
        struct frame_header header = make_header(MSG_LOG_DATA, CLUSTER_MAX_PAYLOAD_BYTES, id);
        CHECK(connection_queue_frame(sender, &header, payload) == 0);
    }
    /* Queueing must copy data, rather than retaining a pointer to this buffer. */
    memset(payload, 0xee, CLUSTER_MAX_PAYLOAD_BYTES);
    free(payload);
    CHECK(connection_flush(sender) == CONNECTION_PROGRESS);
    CHECK(connection_pending_bytes(sender) != 0);
    CHECK(connection_flush(sender) == CONNECTION_AGAIN);
    for (unsigned iteration = 0; iteration < 10000 && received < 8; ++iteration) {
        CHECK(connection_receive(receiver) >= CONNECTION_AGAIN);
        CHECK(connection_flush(sender) >= CONNECTION_AGAIN);
    }
    CHECK(received == 8);
    CHECK(connection_pending_bytes(sender) == 0);
    CHECK(connection_flush(sender) == CONNECTION_AGAIN);
    CHECK(unused.count == 0);
    connection_destroy(sender);
    CHECK(connection_receive(receiver) == CONNECTION_EOF);
    connection_destroy(receiver);
    puts("PASS maximum payload, copied queues, partial writes, backpressure, and ordering");
}

static int count_frames(struct connection *connection, const struct frame_header *header,
                        const uint8_t *payload, void *context)
{
    (void)connection; (void)header; (void)payload;
    ++*(unsigned *)context;
    return 0;
}

static void test_queue_limits(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct captured captured = {0};
    struct connection *connection = connection_create(sockets[0], capture, &captured);
    CHECK(connection != NULL);
    unsigned received = 0;
    struct connection *receiver = connection_create(sockets[1], count_frames, &received);
    CHECK(receiver != NULL);
    uint8_t *payload = calloc(CLUSTER_MAX_PAYLOAD_BYTES, 1);
    CHECK(payload != NULL);
    struct frame_header header = make_header(MSG_LOG_DATA, CLUSTER_MAX_PAYLOAD_BYTES, 1);
    size_t frame_bytes = CLUSTER_HEADER_BYTES + CLUSTER_MAX_PAYLOAD_BYTES;
    unsigned admitted = 0;
    while (connection_queue_frame(connection, &header, payload) == 0) ++admitted;
    CHECK(errno == EAGAIN);
    CHECK(admitted == CLUSTER_CONNECTION_QUEUE_BYTES / frame_bytes);
    size_t pending = connection_pending_bytes(connection);
    CHECK(pending == admitted * frame_bytes);
    CHECK(connection_queue_frame(connection, &header, NULL) == -1 && errno == EINVAL);
    CHECK(connection_pending_bytes(connection) == pending);
    header.payload_length = CLUSTER_MAX_PAYLOAD_BYTES + 1;
    CHECK(connection_queue_frame(connection, &header, payload) == -1 && errno == EINVAL);
    CHECK(connection_pending_bytes(connection) == pending);
    free(payload);
    for (unsigned iteration = 0; iteration < 10000 && received < admitted; ++iteration) {
        CHECK(connection_flush(connection) >= CONNECTION_AGAIN);
        CHECK(connection_receive(receiver) >= CONNECTION_AGAIN);
    }
    CHECK(received == admitted && connection_pending_bytes(connection) == 0);
    header = make_header(MSG_NODES, 0, 2);
    CHECK(connection_queue_frame(connection, &header, NULL) == 0);
    CHECK(connection_flush(connection) == CONNECTION_PROGRESS);
    CHECK(connection_receive(receiver) == CONNECTION_PROGRESS);
    CHECK(received == admitted + 1);
    connection_destroy(connection);
    connection_destroy(receiver);
    puts("PASS bounded queue, transactional rejection, and reuse after draining");
}

static void test_io_budget(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    unsigned received = 0;
    struct connection *sender = connection_create(sockets[0], count_frames, &received);
    struct connection *receiver = connection_create(sockets[1], count_frames, &received);
    CHECK(sender != NULL && receiver != NULL);
    struct frame_header header = make_header(MSG_NODES, 0, 1);
    const unsigned total = 12000;
    CHECK(total * CLUSTER_HEADER_BYTES > CLUSTER_CONNECTION_IO_BUDGET);
    for (unsigned i = 0; i < total; ++i) {
        CHECK(connection_queue_frame(sender, &header, NULL) == 0);
    }
    for (unsigned iteration = 0; iteration < 10000 && received < total; ++iteration) {
        size_t pending = connection_pending_bytes(sender);
        CHECK(connection_flush(sender) >= CONNECTION_AGAIN);
        CHECK(pending - connection_pending_bytes(sender) <= CLUSTER_CONNECTION_IO_BUDGET);
        unsigned previous = received;
        CHECK(connection_receive(receiver) >= CONNECTION_AGAIN);
        CHECK(received - previous <= CLUSTER_CONNECTION_IO_BUDGET / CLUSTER_HEADER_BYTES + 1);
        if (iteration == 0) CHECK(connection_pending_bytes(sender) > 0);
    }
    CHECK(received == total && connection_pending_bytes(sender) == 0);
    connection_destroy(sender);
    connection_destroy(receiver);
    puts("PASS bounded work per call and continuation across I/O budget boundaries");
}

static int respond(struct connection *connection, const struct frame_header *header,
                   const uint8_t *payload, void *context)
{
    (void)payload;
    (void)context;
    struct frame_header reply = make_header(MSG_END, 0, header->request_id);
    CHECK(connection_queue_frame(connection, &reply, NULL) == 0);
    return 0;
}

static void test_half_close(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct connection *connection = connection_create(sockets[0], respond, NULL);
    CHECK(connection != NULL);
    struct frame_header request = make_header(MSG_NODES, 0, 42);
    uint8_t bytes[24];
    CHECK(protocol_encode_header(&request, bytes) == 0);
    send_small(sockets[1], bytes, sizeof(bytes));
    CHECK(shutdown(sockets[1], SHUT_WR) == 0);
    CHECK(connection_receive(connection) == CONNECTION_EOF);
    CHECK(connection_pending_bytes(connection) == sizeof(bytes));
    CHECK(connection_flush(connection) == CONNECTION_PROGRESS);
    size_t used = 0;
    while (used < sizeof(bytes)) {
        ssize_t count = recv(sockets[1], bytes + used, sizeof(bytes) - used, 0);
        CHECK(count > 0);
        used += (size_t)count;
    }
    struct frame_header reply;
    CHECK(protocol_decode_header(bytes, &reply) == 0);
    CHECK(reply.type == MSG_END && reply.request_id == 42);
    connection_destroy(connection);
    close(sockets[1]);
    puts("PASS response queued by handler and flushed after clean read-side EOF");
}

static int reject_frame(struct connection *connection, const struct frame_header *header,
                        const uint8_t *payload, void *context)
{
    (void)connection; (void)header; (void)payload; (void)context;
    return -1;
}

static void test_failures(void)
{
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct connection *connection = connection_create(sockets[0], reject_frame, NULL);
    CHECK(connection != NULL);
    struct frame_header header = make_header(MSG_NODES, 0, 1);
    uint8_t bytes[24];
    CHECK(protocol_encode_header(&header, bytes) == 0);
    CHECK(connection_queue_frame(connection, &header, NULL) == 0);
    send_small(sockets[1], bytes, sizeof(bytes));
    CHECK(connection_receive(connection) == CONNECTION_FATAL && errno == EPROTO);
    CHECK(connection_pending_bytes(connection) == 0 && connection_fd(connection) == -1);
    CHECK(connection_queue_frame(connection, &header, NULL) == -1 && errno == EBADF);
    connection_destroy(connection);
    close(sockets[1]);

    CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    struct captured captured = {0};
    connection = connection_create(sockets[0], capture, &captured);
    CHECK(connection != NULL);
    CHECK(connection_queue_frame(connection, &header, NULL) == 0);
    close(sockets[1]);
    /* SIGPIPE must not terminate this test process when the peer is gone. */
    CHECK(signal(SIGPIPE, SIG_DFL) != SIG_ERR);
    CHECK(connection_flush(connection) == CONNECTION_FATAL);
    CHECK(connection_fd(connection) == -1 && connection_pending_bytes(connection) == 0);
    connection_destroy(connection);

    CHECK(socketpair(AF_UNIX, SOCK_DGRAM, 0, sockets) == 0);
    CHECK(connection_create(sockets[0], capture, &captured) == NULL);
    CHECK(fcntl(sockets[0], F_GETFD) >= 0); /* Failed create retains caller ownership. */
    close(sockets[0]);
    close(sockets[1]);
    CHECK(connection_create(-1, capture, &captured) == NULL);
    CHECK(connection_receive(NULL) == CONNECTION_FATAL);
    CHECK(connection_flush(NULL) == CONNECTION_FATAL);
    CHECK(connection_queue_frame(NULL, &header, NULL) == -1);
    connection_destroy(NULL);
    puts("PASS handler rejection, socket ownership, fatal cleanup, and SIGPIPE suppression");
}

int main(void)
{
    test_header();
    test_fragmented_and_combined();
    test_bad_frames();
    test_backpressure();
    test_queue_limits();
    test_io_budget();
    test_half_close();
    test_failures();
    puts("All transport tests passed.");
    return EXIT_SUCCESS;
}
