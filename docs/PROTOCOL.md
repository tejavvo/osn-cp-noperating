# Shared contract, version 1

All three programs include `common/common.h` and compile with `-std=c11 -Iinclude`. Individual headers are also usable on their own. These are declarations and a wire specification. The functions still need implementations under `src/common/`, owned by C in the work plan. No external library is required by these headers.

This is the team's chosen protocol, not a protocol imposed by the course. Keep it identical on every branch. Changes to message numbers or wire layouts require an agreed protocol version change. Adding later-milestone message IDs without changing existing layouts can keep version 1 if all peers support those new messages.

## Where to look

| Header | Contents |
| --- | --- |
| `common/types.h` | Limits, ID types, states, resources, job specification/result, validation declarations |
| `common/messages.h` | Fixed message IDs, payload structures and `cluster_message_t` |
| `common/protocol.h` | Encode/decode declarations and frame limits |
| `common/net.h` | Blocking TCP, whole-message I/O and UDP discovery declarations |
| `common/log.h` | The single timestamped `log_line()` declaration |

The structs describe values in host memory. Every string array includes room for a trailing NUL, and every chunk owns its bytes. Zero-initialize messages. Never write these structs directly to a socket or depend on `sizeof`, padding or enum sizes for serialization.

## Values and validation

- All IDs are positive. Zero means absent/unassigned. Job/session/request IDs are unsigned 64-bit; worker IDs are unsigned 32-bit. IDs are unique within a scheduler run, never reused within that run. Re-registering a worker gives it a new ID. Scheduler restarts begin a new in-memory system; this protocol does not persist job state.
- Usernames are 1-31 ASCII letters, digits, `_`, `-` or `.`. Empty names, other characters and embedded NULs are invalid. Duplicate usernames are accepted; sessions remain distinct.
- RAM is measured in units of 1,048,576 bytes. Resource cores and GPUs use unsigned 32-bit counts; RAM uses unsigned 64-bit counts. Job cores, RAM and wall-clock seconds must be positive; GPUs may be zero; priority is 1, 2 or 3. Reject RAM/time values that cannot be converted to the target platform's limit/timer types without overflow.
- Executable strings are 1-4095 bytes. `EXEC_PATH` requires a bare name with no `/`, resolved through the worker's PATH. `EXEC_FILE` requires a path containing `/`, which the scheduler reads. M1 has no job-argument list. The submitted path never authorizes a worker to write a file at that path.
- Worker registration requires positive cores/RAM and already-clamped real capacity; GPUs may be zero. Validate registration on the scheduler too. In node replies, each free resource is at most its total.
- Enum values must be defined in the header. M1 only executes `QUEUED -> RUNNING -> COMPLETED/FAILED`; failures before startup can go directly from QUEUED to FAILED. Future lifecycle states are declared now without implementing their behaviors.
- A completed job has `RESULT_EXIT` and exit code 0. Nonzero exit, signal, timeout, memory, setup or transfer errors mean FAILED. Before completion, result kind is NONE and other result fields are zero/empty. For EXIT, signal number is zero; for SIGNAL, exit code is zero; for other kinds both numbers are zero. Timeout detail uses `Wall-clock timeout exceeded (<n>s limit)`, including the course's CPU-limit timeout case.
- Detail strings contain at most 255 bytes. Paths/details are uninterpreted non-NUL bytes; do not assume a particular Unicode encoding. On decode, append a NUL locally. Unknown state/error names return the literal `UNKNOWN` from the name helpers; other invalid arguments return `CLUSTER_ERR_ARGUMENT`.

Validators check these value rules, not filesystem access, actual capacity or a job's legal state transition. The owning program checks those contextual conditions. `cluster_validate_job_spec()` writes a NUL-terminated explanation on failure when a nonempty output buffer is supplied; success writes an empty string. Shared helpers do not call `exit()`.

## TCP frame layout

Integers use network byte order, most significant byte first, including 64-bit values. C enums are encoded as `u32` except the envelope's type, encoded as `u16`. No alignment padding exists on the wire.

| Offset | Field | Bytes | Meaning |
| --- | --- | --- | --- |
| 0 | body_length | 4 | Remaining frame length, excluding this field |
| 4 | version | 2 | Exactly 1 |
| 6 | type | 2 | Message ID 1-20 |
| 8 | flags | 4 | Zero, or MORE where allowed |
| 12 | request_id | 8 | Request/reply correlation or zero for unsolicited messages |
| 20 | payload | Variable | Defined below |

The complete frame is at most 65,536 bytes, so `16 <= body_length <= 65,532`. Every payload must consume exactly the remaining bytes. Reject oversized, truncated or trailing data, unknown versions/types and unknown flag bits. Check bounds before copying or allocating. Close a connection after a framing/protocol error. The decode routine also rejects complete frames whose payload values violate the rules above.

`S(max)` means a `u32` byte length followed by exactly that many bytes, excluding NUL. Its declared maximum does not include a terminator. `B` means a `u32` length followed by that many arbitrary bytes, at most 16,384. For B, send only `data_length` bytes, not the full in-memory array.

Reusable layouts, in this exact order:

```text
R       = u32 cores, u64 ram_mb, u32 gpus
SPEC    = S(4095) executable, u32 execution_mode, R resources,
          u32 time_limit_seconds, u8 priority
RESULT  = u32 kind, u32 exit_code, u32 signal_number, S(255) detail
NODE    = u32 worker_id, u32 state, R total, R free, u32 running_jobs
JOB     = u64 job_id, u32 state, u32 worker_id, S(31) username, RESULT result
CHUNK   = u64 job_id, u64 offset, B data
```

Message payloads follow this table, not the compiler's struct layout:

| ID | Message | Payload in wire order |
| --- | --- | --- |
| 1 | LOGIN | `S(31) username` |
| 2 | LOGIN_REPLY | `u64 session_id, S(31) username` |
| 3 | REGISTER | `R capacity` |
| 4 | REGISTER_REPLY | `u32 worker_id` |
| 5 | SUBMIT | `SPEC` |
| 6 | SUBMIT_REPLY | `u64 job_id, u32 state` |
| 7 | NODES | Empty |
| 8 | NODES_REPLY | `u32 count`, then `count` NODE entries; count is 0-32 |
| 9 | STATUS | `u64 job_id` |
| 10 | STATUS_REPLY | `JOB` |
| 11 | LOGS | `u64 job_id` |
| 12 | LOGS_REPLY | `CHUNK` |
| 13 | ASSIGN | `u64 job_id, u32 worker_id, SPEC, u64 binary_size` |
| 14 | BINARY_CHUNK | `CHUNK` |
| 15 | BINARY_END | `u64 job_id, u64 binary_size` |
| 16 | JOB_STARTED | `u32 worker_id, u64 job_id` |
| 17 | OUTPUT | `u32 worker_id, CHUNK` |
| 18 | JOB_DONE | `u32 worker_id, u64 job_id, u32 state, RESULT` |
| 19 | EVENT | `u64 job_id, u32 state, u32 worker_id, S(255) detail` |
| 20 | ERROR | `u32 code, u64 job_id, u32 worker_id, S(255) detail` |

`payload.job_query` represents both STATUS and LOGS. SUBMIT uses `payload.submit` directly. NODES has no active union member. Other union member names match the table, except REGISTER uses `payload.registration`.

## Requests, replies and sessions

Clients send LOGIN as their first application message. Workers instead send REGISTER first; no separate role handshake precedes either message. LOGIN, REGISTER, SUBMIT, NODES, STATUS and LOGS carry nonzero request IDs, unique within the connection. Their success replies echo that ID. On rejection, ERROR echoes it instead of a success reply. The generic QUEUE_FULL error is `ERROR` with code `CLUSTER_ERROR_QUEUE_FULL`, job ID zero and no accepted job.

ASSIGN, binary transfer, JOB_STARTED, OUTPUT, JOB_DONE and EVENT are unsolicited and use request ID zero. Asynchronous errors may also use zero. An ERROR from a registered worker includes its worker ID; other senders use zero. A failure of an assigned job should use JOB_DONE with a failure result rather than a generic ERROR. Validate worker IDs against the registered connection, not just the payload.

SUBMIT_REPLY initially reports QUEUED. Enqueue that reply before the job's first EVENT to the same client. Derive the job's username and submitting session from the connection, never from a submit payload. Route job events only to that submitting session, including all its job state changes. Other sessions with the same username are accepted but do not automatically receive those events. Queries need not be limited to the owner; the PDF explicitly restricts event delivery, not query access.

On reconnect, the client repeats LOGIN with its saved username and gets a new session. M1 does not recover subscriptions or replay missed events. It must not automatically resend an unacknowledged SUBMIT, because the old request might already have created a job. Tell the user the submission outcome is unknown instead. The user can query existing jobs if they know their IDs. Worker reconnects kill/reap old processes and repeat discovery and REGISTER for a fresh worker ID.

`MORE` is allowed only on NODES_REPLY and LOGS_REPLY. All pieces have the same request ID; the final piece clears MORE. A zero-worker response sends one NODES_REPLY with count zero. Snapshot node data at the start of the request so its pieces are consistent. Other requests/events can be interleaved between pieces and are distinguished by request ID/type.

Plain LOGS takes a snapshot ending at the output position known when the request is handled. Send up to 16,384 bytes per reply, with absolute offsets and MORE until the last piece. An empty snapshot sends one reply with zero bytes and no MORE. If retaining only a tail, the first offset may be nonzero; the client should say earlier bytes were discarded. No live follow subscription is defined in M1.

## Assignments, output and results

For FILE jobs, scheduler sends ASSIGN with the exact file size, zero or more BINARY_CHUNK frames and one BINARY_END with the same size. The first chunk offset is zero, each next offset equals bytes already received, and final bytes received must equal the declared size. CHUNK lengths are positive except empty LOGS replies. Check size additions for overflow. Different jobs' frames can interleave; track received bytes per job. No caching/hash protocol is defined yet.

The worker stages received files under a worker-controlled name, copies/prepares the executable in `/tmp/osn_job_<JobID>`, checks permissions and completes the runner's setup. Never use the submitted path as a destination. An empty transferred file will fail to execute normally; it must not hang waiting for a chunk.

For PATH jobs, ASSIGN has `binary_size=0` and no binary frames follow. Worker executes the bare command through `execvp()`. For either mode, send JOB_STARTED only after successful exec startup, using a close-on-exec error pipe to distinguish exec/setup failure from a successfully launched child. Start the wall-clock limit when execution begins, not during network transfer.

Scheduler reserves resources when accepting a dispatch, but exposes RUNNING after JOB_STARTED. Until then the job remains QUEUED with an internal dispatch-pending marker; it cannot be selected again and still counts against queue capacity. Keep this marker in scheduler-local state, not a new wire lifecycle state. On success remove it from the pending queue. On pre-start failure release the reservation and mark FAILED. If a connection breaks during dispatch, resolve its reservation once and report a clear failure; heartbeat-driven retry is M2.

OUTPUT offsets start at zero for each job and advance by exactly the preceding data length, using the merged stdout/stderr log. Send new bytes roughly every 500 ms. On exit, send all remaining OUTPUT before JOB_DONE. For jobs with no output, send no OUTPUT. JOB_DONE is sent exactly once, including setup failures before JOB_STARTED. A reserves/releases resources once per dispatch and ignores/rejects duplicate terminal updates; it validates the sender owns that job. Report output transfer failure as a connection failure rather than declaring successful delivery. Directory cleanup follows successful final reporting, with best-effort cleanup on disconnect.

When B drops privileges or calls `execvp()` in a child of a threaded worker, it reports setup errors to the parent using a pipe. The child must not call the shared mutex-based logger.

## Discovery datagrams

Discovery uses IPv4 UDP broadcast on the configured discovery port. Numeric fields use network byte order; there is no TCP length prefix.

```text
REQUEST, exactly 12 bytes:
    4 bytes ASCII "OSND", u16 version=1, u16 kind=1, u32 nonce

REPLY, exactly 18 bytes:
    4 bytes ASCII "OSND", u16 version=1, u16 kind=2, u32 same_nonce,
    4 bytes reachable scheduler IPv4 address, u16 scheduler TCP port
```

Use a fresh nonce per attempt; accept only a matching current reply with a valid nonzero TCP port and reachable non-wildcard address. Pick the first valid reply. The scheduler selects a local interface address reachable from the request sender. Bind its TCP listener on all IPv4 interfaces. Broadcast using SO_BROADCAST; support the active interfaces on Linux/macOS without requiring users to configure an address. The responder reuses its UDP bind with SO_REUSEADDR/SO_REUSEPORT as supported.

Each attempt waits at most two seconds total, regardless of irrelevant datagrams. After three unsuccessful attempts, return the supplied fallback with source FALLBACK, or ERR_NO_SCHEDULER when absent. Do not switch to fallback early merely because it is provided. Do not retry an entire discovery cycle forever inside this helper. The caller owns reconnect policy and logs the found/fallback outcome through `log_line()`.

## Function usage and implementation ownership

```c
#include "common/common.h"

/* Example inside a client's send path. Implementations are still required. */
cluster_message_t request = {0};
request.type = CLUSTER_MSG_STATUS;
request.request_id = 1;
request.payload.job_query.job_id = 7;
/* cluster_send_message(scheduler_fd, &request); */
```

C implements types/validation, codecs, TCP helpers, discovery and logger. A/B use the same headers while writing module logic or fake peers. Full linking waits for C's implementations. There are no pretend success-returning stubs.

The TCP helpers are blocking by design, fitting A's planned thread-per-connection scheduler. C's interactive client should use a dedicated reader thread with these helpers so a partial incoming frame cannot freeze stdin. B's job monitoring remains separate from its blocking receive path. One reader owns each connection; serialize complete send calls, including those made by output callbacks. Never hold scheduler table locks during these blocking calls.

`cluster_message_encode/decode()` own no heap memory. `cluster_send_message/recv_message()` may use internal bounded scratch storage but return no allocated memory. `recv_message()` zeroes its output on failure. All other output values are unspecified on failure except where a header explicitly guarantees zero or -1. Invalid arguments return ERR_ARGUMENT. Transport/responder I/O errors return ERR_IO with the system errno preserved; protocol failures return ERR_PROTOCOL. Clean EOF is returned only between frames. On a failed send/receive, close the socket rather than resuming an uncertain frame.

For C1's commit, include `include/common/`, this protocol note, and the README/plan pointers. Suggested message: `Define shared job types and message interfaces`.
