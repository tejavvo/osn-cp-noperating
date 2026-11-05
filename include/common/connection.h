#ifndef CLUSTER_CONNECTION_H
#define CLUSTER_CONNECTION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common/protocol.h"

/* Limits frame storage and work per call so one connection cannot monopolize
 * memory or an event loop. The queue cap includes headers and sent prefixes.
 */
#define CLUSTER_CONNECTION_QUEUE_BYTES (1024u * 1024u)
#define CLUSTER_CONNECTION_IO_BUDGET (256u * 1024u)

enum connection_result {
    CONNECTION_FATAL = -2,
    CONNECTION_EOF = -1,
    CONNECTION_AGAIN = 0,
    CONNECTION_PROGRESS = 1
};

struct connection;

/* Payload is borrowed for this call only; it is NULL for empty frames.
 * Return 0 to continue, nonzero to reject the frame and close the connection.
 * A handler may queue responses. It must not recursively receive/flush or
 * destroy the connection. Serialize all access from the owning thread.
 */
typedef int (*connection_frame_handler)(struct connection *connection,
                                        const struct frame_header *header,
                                        const uint8_t *payload, void *context);

/* Wrap an already-connected stream socket, enable O_NONBLOCK and suppress
 * SIGPIPE locally. On success the connection owns fd. On failure fd remains
 * caller-owned and open. handler is required; context may be NULL.
 */
struct connection *connection_create(int fd, connection_frame_handler handler,
                                     void *context);
void connection_destroy(struct connection *connection);
int connection_fd(const struct connection *connection);
size_t connection_pending_bytes(const struct connection *connection);

/* Copies the complete frame. Returns 0 on success or -1 with errno set:
 * EINVAL for bad input, EAGAIN for a full queue, ENOMEM for allocation failure,
 * EBADF for a closed connection. Failure leaves the queue unchanged.
 */
int connection_queue_frame(struct connection *connection,
                           const struct frame_header *header,
                           const uint8_t *payload);

/* Return enum connection_result values. Retain progress across EAGAIN and
 * retry EINTR. PROGRESS may leave unread data or pending writes due to the I/O
 * budget. AGAIN means no work was done. EOF means a clean read-side shutdown;
 * queued writes may still be flushed. EOF/FATAL take precedence over PROGRESS
 * when encountered, even if handlers already ran during that call.
 * FATAL sets errno, closes the fd and releases buffers, but leaves the object
 * for the caller to destroy. Truncated frames and handler rejection use EPROTO.
 * Poll for reading and, only when pending_bytes > 0, writing. With edge-triggered
 * polling, call again after PROGRESS until AGAIN/EOF/FATAL is returned.
 */
int connection_receive(struct connection *connection);
int connection_flush(struct connection *connection);

#endif
