#ifndef CLUSTER_COMMON_NET_H
#define CLUSTER_COMMON_NET_H

#include "common/protocol.h"

/* TCP helpers use blocking sockets. Return status; descriptors via output args.
 * On failure, output descriptors are -1. Caller closes successful descriptors.
 * Listen binds INADDR_ANY with SO_REUSEADDR; accept retries EINTR. Helpers must
 * not terminate the program or print application-specific messages.
 */
cluster_status_t cluster_tcp_connect(const cluster_endpoint_t *endpoint,
                                     int *out_fd);
cluster_status_t cluster_tcp_listen(uint16_t port, int backlog, int *out_fd);
cluster_status_t cluster_tcp_accept(int listen_fd, int *out_fd);

/* Complete-frame blocking I/O, including EINTR and short reads/writes.
 * recv returns EOF only before any byte of a new frame, ERR_PROTOCOL for
 * mid-frame EOF or invalid framing. Never allocate from an unchecked length.
 * send must suppress SIGPIPE on Linux/macOS and return ERR_IO on broken pipes.
 * Exactly one reader per fd. Caller serializes COMPLETE send_message calls
 * using a per-connection mutex or one writer; helpers do not lock the socket.
 * On failure, close the connection rather than retrying a half-sent frame.
 * Do not call on nonblocking fds or while holding a scheduler table lock.
 */
cluster_status_t cluster_send_message(int fd, const cluster_message_t *message);
cluster_status_t cluster_recv_message(int fd, cluster_message_t *message);

typedef enum {
    CLUSTER_DISCOVERED_UDP = 1,
    CLUSTER_DISCOVERED_FALLBACK = 2
} cluster_discovery_source_t;

typedef struct {
    uint16_t discovery_port;
    int has_fallback;          /* Exactly 0 or 1. */
    cluster_endpoint_t fallback;
} cluster_discovery_options_t;

/* Tries discovery first: three broadcasts, each with a 2000 ms deadline.
 * Ignore malformed/unmatched replies without extending the attempt deadline.
 * Use fallback only after exhausting attempts, or ERR_NO_SCHEDULER if absent.
 * Invalid arguments return ERR_ARGUMENT. Zero endpoint/source on failure.
 * Caller logs outcome and calls again after disconnect; no reconnect loop here.
 */
cluster_status_t discover_scheduler(const cluster_discovery_options_t *options,
                                    cluster_endpoint_t *endpoint,
                                    cluster_discovery_source_t *source);

/* Open UDP responder on INADDR_ANY with SO_REUSEADDR and SO_REUSEPORT, where
 * supported. Caller owns fd and runs responder in its own thread/event loop.
 * respond_once handles one valid request, replying with a reachable local IPv4
 * address and scheduler_tcp_port. It does not advertise 0.0.0.0 or loopback to
 * remote peers. timeout_ms >= 0; return ERR_TIMEOUT if no valid request arrives
 * by deadline. Invalid datagrams are ignored without resetting that deadline.
 */
cluster_status_t cluster_discovery_open(uint16_t discovery_port, int *out_fd);
cluster_status_t cluster_discovery_respond_once(int fd,
                                               uint16_t scheduler_tcp_port,
                                               uint32_t timeout_ms);

#endif
