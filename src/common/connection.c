#include "common/connection.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct outgoing_frame {
    struct outgoing_frame *next;
    size_t size;
    size_t sent;
    uint8_t data[];
};

struct connection {
    int fd;
    bool read_eof;
    connection_frame_handler handler;
    void *context;
    uint8_t header_bytes[CLUSTER_HEADER_BYTES];
    size_t header_used;
    struct frame_header header;
    uint8_t *payload;
    size_t payload_used;
    struct outgoing_frame *outgoing_head;
    struct outgoing_frame *outgoing_tail;
    size_t allocated_bytes;
    size_t pending_bytes;
};

static void release_buffers(struct connection *connection)
{
    free(connection->payload);
    connection->payload = NULL;
    while (connection->outgoing_head != NULL) {
        struct outgoing_frame *next = connection->outgoing_head->next;
        free(connection->outgoing_head);
        connection->outgoing_head = next;
    }
    connection->outgoing_tail = NULL;
    connection->pending_bytes = 0;
    connection->allocated_bytes = 0;
}

static int fail_connection(struct connection *connection, int error)
{
    if (connection->fd >= 0) {
        close(connection->fd);
        connection->fd = -1;
    }
    release_buffers(connection);
    errno = error;
    return CONNECTION_FATAL;
}

struct connection *connection_create(int fd, connection_frame_handler handler,
                                     void *context)
{
    if (fd < 0 || handler == NULL) {
        errno = EINVAL;
        return NULL;
    }
    struct connection *connection = calloc(1, sizeof(*connection));
    if (connection == NULL) {
        return NULL;
    }
    int socket_type;
    socklen_t type_length = sizeof(socket_type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &socket_type, &type_length) < 0) {
        goto failed;
    }
    if (socket_type != SOCK_STREAM) {
        errno = EINVAL;
        goto failed;
    }
    /* macOS has SO_NOSIGPIPE; Linux uses MSG_NOSIGNAL on every send below. */
#if defined(SO_NOSIGPIPE)
    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) < 0) {
        goto failed;
    }
#elif !defined(MSG_NOSIGNAL)
#error "This platform needs a socket-local SIGPIPE suppression mechanism"
#endif
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        goto failed;
    }
    connection->fd = fd;
    connection->handler = handler;
    connection->context = context;
    return connection;

failed: {
        int error = errno;
        free(connection);
        errno = error;
        return NULL;
    }
}

void connection_destroy(struct connection *connection)
{
    if (connection != NULL) {
        if (connection->fd >= 0) {
            close(connection->fd);
        }
        release_buffers(connection);
        free(connection);
    }
}

int connection_fd(const struct connection *connection)
{
    return connection == NULL ? -1 : connection->fd;
}

size_t connection_pending_bytes(const struct connection *connection)
{
    return connection == NULL ? 0 : connection->pending_bytes;
}

int connection_queue_frame(struct connection *connection,
                           const struct frame_header *header,
                           const uint8_t *payload)
{
    uint8_t encoded[CLUSTER_HEADER_BYTES];
    if (connection == NULL || header == NULL ||
        (header->payload_length != 0 && payload == NULL) ||
        protocol_encode_header(header, encoded) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (connection->fd < 0) {
        errno = EBADF;
        return -1;
    }
    size_t size = CLUSTER_HEADER_BYTES + (size_t)header->payload_length;
    if (size > CLUSTER_CONNECTION_QUEUE_BYTES - connection->allocated_bytes) {
        errno = EAGAIN;
        return -1;
    }
    struct outgoing_frame *frame = malloc(sizeof(*frame) + size);
    if (frame == NULL) {
        return -1;
    }
    frame->next = NULL;
    frame->size = size;
    frame->sent = 0;
    memcpy(frame->data, encoded, CLUSTER_HEADER_BYTES);
    if (header->payload_length != 0) {
        memcpy(frame->data + CLUSTER_HEADER_BYTES, payload, header->payload_length);
    }
    if (connection->outgoing_tail != NULL) {
        connection->outgoing_tail->next = frame;
    } else {
        connection->outgoing_head = frame;
    }
    connection->outgoing_tail = frame;
    connection->allocated_bytes += size;
    connection->pending_bytes += size;
    return 0;
}

int connection_receive(struct connection *connection)
{
    if (connection == NULL || connection->fd < 0) {
        errno = EBADF;
        return CONNECTION_FATAL;
    }
    if (connection->read_eof) {
        return CONNECTION_EOF;
    }
    size_t processed = 0;
    while (processed < CLUSTER_CONNECTION_IO_BUDGET) {
        bool reading_header = connection->header_used < CLUSTER_HEADER_BYTES;
        uint8_t *destination;
        size_t remaining;
        if (reading_header) {
            destination = connection->header_bytes + connection->header_used;
            remaining = CLUSTER_HEADER_BYTES - connection->header_used;
        } else {
            destination = connection->payload + connection->payload_used;
            remaining = connection->header.payload_length - connection->payload_used;
        }
        size_t budget = CLUSTER_CONNECTION_IO_BUDGET - processed;
        if (remaining > budget) {
            remaining = budget;
        }
        ssize_t count = recv(connection->fd, destination, remaining, 0);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return processed == 0 ? CONNECTION_AGAIN : CONNECTION_PROGRESS;
            }
            return fail_connection(connection, errno);
        }
        if (count == 0) {
            if (connection->header_used != 0) {
                return fail_connection(connection, EPROTO);
            }
            connection->read_eof = true;
            return CONNECTION_EOF;
        }
        processed += (size_t)count;
        if (reading_header) {
            connection->header_used += (size_t)count;
            if (connection->header_used != CLUSTER_HEADER_BYTES) {
                continue;
            }
            if (protocol_decode_header(connection->header_bytes, &connection->header) < 0) {
                return fail_connection(connection, errno);
            }
            if (connection->header.payload_length != 0) {
                connection->payload = malloc(connection->header.payload_length);
                if (connection->payload == NULL) {
                    return fail_connection(connection, ENOMEM);
                }
            }
        } else {
            connection->payload_used += (size_t)count;
        }
        if (connection->payload_used == connection->header.payload_length) {
            if (connection->handler(connection, &connection->header,
                                    connection->payload, connection->context) != 0) {
                return fail_connection(connection, EPROTO);
            }
            free(connection->payload);
            connection->payload = NULL;
            connection->header_used = 0;
            connection->payload_used = 0;
        }
    }
    return CONNECTION_PROGRESS;
}

int connection_flush(struct connection *connection)
{
    if (connection == NULL || connection->fd < 0) {
        errno = EBADF;
        return CONNECTION_FATAL;
    }
    size_t processed = 0;
    while (connection->outgoing_head != NULL && processed < CLUSTER_CONNECTION_IO_BUDGET) {
        struct outgoing_frame *frame = connection->outgoing_head;
        size_t remaining = frame->size - frame->sent;
        size_t budget = CLUSTER_CONNECTION_IO_BUDGET - processed;
        if (remaining > budget) {
            remaining = budget;
        }
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags = MSG_NOSIGNAL;
#endif
        ssize_t count = send(connection->fd, frame->data + frame->sent, remaining, flags);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return processed == 0 ? CONNECTION_AGAIN : CONNECTION_PROGRESS;
            }
            return fail_connection(connection, errno);
        }
        if (count == 0) {
            return fail_connection(connection, EPIPE);
        }
        processed += (size_t)count;
        frame->sent += (size_t)count;
        connection->pending_bytes -= (size_t)count;
        if (frame->sent == frame->size) {
            connection->outgoing_head = frame->next;
            if (connection->outgoing_head == NULL) {
                connection->outgoing_tail = NULL;
            }
            connection->allocated_bytes -= frame->size;
            free(frame);
        }
    }
    return processed == 0 ? CONNECTION_AGAIN : CONNECTION_PROGRESS;
}
