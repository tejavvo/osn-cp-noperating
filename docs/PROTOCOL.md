# Distributed compute cluster protocol

Version: 1. Status: implementation contract. Startup argument parsing, header
encoding/decoding, and buffered connection I/O are implemented. Message payload
encoding and application networking are subsequent steps.

This document defines the team's wire format and shared behavior. The course
brief remains the authority for assessed behavior. Choices such as message IDs,
encoding, limits, and same-username handling below are team design decisions.
Implement Milestone 1 first. Later messages are specified so the three programs
can share definitions without implementing every feature immediately.

## 1. Programs and connections

Build three C executables in the repository root:

```text
./scheduler <port> <FIFO|PRIORITY> [-discovery-port <port>]
./worker [<scheduler_ip> <scheduler_port>] -cores <num> -ram <MB> [-gpu <num>] [-discovery-port <port>]
./client [<scheduler_ip> <scheduler_port>] [-user <username>] [-discovery-port <port>]
```

- The scheduler owns IDs, users, jobs, the queue, assignments, and resource accounting.
- Each worker maintains one persistent TCP connection to the scheduler. It owns
  its child processes, execution limits, output capture, and local files.
- Each client maintains one persistent TCP connection to the scheduler. It sends
  commands and receives responses, asynchronous events, and requested logs.
- Clients and workers never connect directly. Executable bytes travel from the
  scheduler's filesystem to workers, not from the client over its connection.
- UDP discovery defaults to port 9999. TCP uses the scheduler's positional port.
- No passwords, encryption, scheduler persistence, or cross-platform binary
  translation are provided. All processes in a cluster must use protocol v1.

## 2. Encoding and limits

All integers are unsigned, big-endian values. Encode each field explicitly.
Never send a C struct, pointer, enum object, `size_t`, or native floating-point
value directly. C struct padding is not part of this protocol.

| Notation | Encoding |
| --- | --- |
| `u8`, `u16`, `u32`, `u64` | 1, 2, 4, or 8 bytes |
| `bool` | `u8`, exactly 0 or 1 |
| `str` | `u32` byte length followed by UTF-8 bytes, without a NUL terminator |
| `bytes` | `u32` byte length followed by arbitrary bytes |
| `list<T>` | `u32` element count followed by that many encoded elements |

Payload fields appear in the exact order listed in this document. Composite
records expand in place. Empty payload means zero bytes. Decoders reject missing
fields, trailing bytes, invalid enum values, and invalid lengths before use.
Reject NUL bytes in strings. Job output and checkpoint contents are `bytes`.

| Constant | Value |
| --- | --- |
| Maximum TCP payload | 65,536 bytes, excluding the header |
| Maximum file/output chunk data | 60,000 bytes |
| Maximum command/path string | 4,096 bytes |
| Maximum username | 31 ASCII bytes |
| Maximum diagnostic string | 1,024 bytes |
| Maximum dependencies in a submit request | 1,024 references |
| Maximum checkpoint | 60,000 bytes |
| Scheduler retained output per job | At least 65,536 bytes |
| Submission queue capacity | 1,024 entries |
| MB conversion | 1 MB = 1,048,576 bytes |

Other strings have a 1,024-byte limit unless a smaller limit is stated.
Check multiplication, addition, and length limits before allocating or converting
resource amounts. Lists must fit within the payload limit. Large listings and
cache inventories use multiple frames, never a larger frame.

## 3. TCP header and correlation

Every frame begins with this 24-byte header:

| Offset | Field | Type | Meaning |
| --- | --- | --- | --- |
| 0 | magic | u32 | `0x4f534e43`, ASCII `OSNC` |
| 4 | version | u16 | 1 |
| 6 | type | u16 | Message ID from section 7 |
| 8 | payload_length | u32 | Bytes after this header |
| 12 | request_id | u32 | Request/response correlation, or 0 for unsolicited frames |
| 16 | job_id | u32 | Target job, or 0 when not applicable |
| 20 | run_id | u32 | Execution generation, or 0 for client-facing/nonexecution frames |

Each requester allocates nonzero request IDs, unique among its outstanding
requests on that connection. A response echoes the request ID. Events and worker
execution reports use 0. Scheduler-to-worker execution commands also use 0;
their responses correlate through job ID and run ID.

Job IDs and cron template IDs each start at 1 in separate namespaces. Worker IDs
start at 1001. IDs are never reused during one scheduler process. Refuse an
allocation on integer exhaustion. Restarting the scheduler starts a new cluster
session; reconnecting clients must not replay old job commands automatically.

The scheduler increments a job's `run_id` for every assignment, including restarts
after preemption or migration. This is distinct from `attempt`, which starts at 1
and increases only when recovering from worker failure. Default maximum attempts
is 3, a code constant. A stale report must not finish a newer run, append its
output, replace its checkpoint, or release its resources.

Readers must handle partial headers, partial payloads, and multiple frames per
read. Writers must handle partial writes and `EINTR`. EOF midway through a frame
is a failed connection. An incremental receiver must not block other sockets
while waiting for the rest of a frame. Each connection has one serialized writer
or outgoing queue. Do not hold scheduler state locks during network or disk I/O.
Avoid process termination from `SIGPIPE` on Linux and macOS.

Close a connection with invalid magic, unsupported version, oversized payload,
or malformed encoding. Valid but unavailable operations return `UNSUPPORTED`.
Reject messages from the wrong connection role with `BAD_STATE`.

## 4. UDP discovery

Discovery uses IPv4 UDP datagrams, not the TCP header. Each datagram must be
exactly the size below, with big-endian integer fields.

```text
Request, 12 bytes:
  magic:u32 = 0x4f534e44       # ASCII OSND
  version:u16 = 1
  kind:u16 = 1
  nonce:u32

Reply, 18 bytes:
  magic:u32 = 0x4f534e44
  version:u16 = 1
  kind:u16 = 2
  nonce:u32                   # echoes the request
  tcp_ipv4:4 raw network-order address bytes
  tcp_port:u16
```

Enable `SO_BROADCAST` for requests. Bind the scheduler responder with
`SO_REUSEADDR` and `SO_REUSEPORT` on Linux/macOS before binding. Reply by unicast
to the request source. Advertise a reachable address on the receiving interface,
never `0.0.0.0`. Use packet interface metadata or route-based address selection
when the scheduler has multiple interfaces.

At startup and after disconnect, clients and workers broadcast up to three
requests, each with a fresh nonce and a two-second reply window. Ignore malformed
or unrelated datagrams. Try valid replies within the window; an unreachable TCP
endpoint does not count as a successful discovery. Use a supplied fallback
address only after all discovery attempts fail. Without a fallback, report an
error and exit. Log discovery and fallback outcomes.

## 5. Shared records and numeric values

```text
Resources = cores:u32, ram_mb:u32, gpu:u32
  cores and ram_mb must be positive; gpu may be zero.

JobState:u8
  QUEUED=1, RUNNING=2, COMPLETED=3, FAILED=4, CANCELLED=5,
  WAITING=6, LOST=7, FAILED_PERMANENT=8

WorkerState:u8
  ALIVE=1, SUSPECTED=2, DEAD=3, DRAINING=4

Dependency = kind:u8, id:u32
  kind JOB=1 or GROUP=2; id must be positive.

JobSpec = command:str, resources:Resources, time_sec:u32, priority:u8,
          preemptible:bool, group_id:u32, every_sec:u32,
          dependencies:list<Dependency>
  command is nonempty; time_sec is positive; priority is 1..3.
  group_id=0 means no group; every_sec=0 means an ordinary job.
  preemptible=false, group_id=0, every_sec=0 and an empty dependency list
  are the Milestone 1 defaults. Reject nondefault later fields until supported.

Telemetry = ram_bytes:u64, peak_ram_bytes:u64, page_faults:u64, cpu_usec:u64
  Unavailable fields use UINT64_MAX, not zero. Zero is a real measurement.

Status:u16
  OK=0, INVALID_ARGUMENT=1, NOT_FOUND=2, QUEUE_FULL=3, BAD_STATE=4,
  UNSUPPORTED=5, IO_ERROR=6, INTERNAL_ERROR=7, PERMISSION_DENIED=8

FinishReason:u8
  EXITED=1, SIGNALLED=2, TIMEOUT=3, MEMORY_LIMIT=4, CANCELLED=5,
  PREEMPTED=6, MIGRATED=7, START_ERROR=8

StopReason:u8
  CANCEL=1, PREEMPT=2, MIGRATE=3
```

Use monotonic clocks for aging, deadlines, heartbeat timeouts, and checkpoint
age. Transmit durations rather than host monotonic timestamps. CPU load uses
`u32` hundredths of a percent, 0..10000, or UINT32_MAX when unavailable.

## 6. Login, registration, and feature support

The client's first frame is `CLIENT_LOGIN`, containing its username. A valid
username matches `[A-Za-z0-9_.-]{1,31}`. The scheduler returns `WELCOME` before
accepting commands. The worker's first frame is `WORKER_REGISTER`; its `WELCOME`
assigns the worker ID. Registration capacity must already be clamped to actual
cores and RAM. GPU capacity is an unclamped counter.

`features:u32` is a bitmask: CORE=1, RESILIENCE=2, ADVANCED=4, CHECKPOINTS=8.
CORE is required. Each later bit requires all earlier bits. `WELCOME` returns
the intersection of requester and scheduler features. Scheduling may assign a
job only to a worker with the features its execution requires. Implementations
return `UNSUPPORTED` for later operations rather than silently ignoring flags.

Username is the user identity. Two connections with the same name are allowed
and share ownership. Deliver owner events to every currently connected session
with that name. Queries and logs may inspect any job; cancellation, migration,
and cron cancellation require the matching owner. This is a project policy,
not password-based authentication. Disconnecting a client does not cancel jobs.

On reconnect, a client sends its saved username again. Do not automatically
resubmit a request whose response was lost; it may already have been accepted.
Subscriptions end on disconnect. A worker kills and reaps its jobs after losing
the scheduler connection, rediscovers the scheduler, and registers under a new ID.

## 7. Message catalog

`C`, `S`, and `W` mean client, scheduler, and worker. All fields below are payload
fields; job ID and run ID belong in the header. Client requests use run ID 0.
Execution messages require positive job and run IDs. Non-job messages use both 0.
`END` terminates a request's response sequence. It has the same job ID as the
request, with run ID 0. A failed request sends `END` with non-OK status and no
success response. A successful singleton reply is also followed by `END`.

### Milestone 1

| ID | Name | Direction | Payload |
| --- | --- | --- | --- |
| 1 | CLIENT_LOGIN | C to S | username:str, features:u32 |
| 2 | WORKER_REGISTER | W to S | capacity:Resources, features:u32 |
| 3 | WELCOME | S to C/W | worker_id:u32, features:u32, heartbeat_ms:u32 |
| 4 | END | S to C/W | status:Status, detail:str |
| 10 | SUBMIT | C to S | spec:JobSpec |
| 11 | SUBMIT_REPLY | S to C | entity_kind:u8, entity_id:u32, state:u8 |
| 12 | NODES | C to S | empty |
| 13 | NODE_ITEM | S to C | worker_id:u32, state:WorkerState, total:Resources, free_cores:u32, free_ram_mb:u32, free_gpu:u32, running_jobs:u32, reserved_jobs:u32, cpu_load:u32 |
| 14 | STATUS | C to S | empty |
| 15 | STATUS_REPLY | S to C | owner:str, state:JobState, worker_id:u32, attempt:u32, priority:u8, effective_priority:u8, spec:JobSpec, telemetry:Telemetry, checkpoint_number:u64, checkpoint_size:u32, checkpoint_age_ms:u64, detail:str |
| 16 | LOGS | C to S | follow:bool |
| 17 | LOG_DATA | S to C | stream_offset:u64, data:bytes |
| 18 | EVENT | S to C | event_kind:u8, worker_id:u32, old_state:u8, new_state:u8, template_id:u32, detail:str |
| 20 | JOB_PREPARE | S to W | owner:str, attempt:u32, spec:JobSpec, execution_mode:u8, executable_name:str, sha256:str, checkpoint_number:u64 |
| 21 | FILE_BEGIN | S to W | kind:u8, size:u64, sha256:str |
| 22 | FILE_CHUNK | S to W | kind:u8, offset:u64, data:bytes |
| 23 | FILE_END | S to W | kind:u8 |
| 24 | JOB_READY | W to S | empty |
| 25 | JOB_START | S to W | empty |
| 26 | JOB_STARTED | W to S | empty |
| 27 | JOB_OUTPUT | W to S | offset:u64, data:bytes |
| 28 | JOB_FINISHED | W to S | reason:FinishReason, exit_code:u32, signal_number:u32, telemetry:Telemetry, detail:str |
| 29 | JOB_REJECTED | W to S | status:Status, detail:str |

`WELCOME.worker_id` is 0 for clients; `heartbeat_ms` is 2000. Both greetings use
nonzero request IDs and receive `WELCOME` then `END`. A failed greeting receives
`END` then disconnection. Client commands must wait for successful greeting END.

`SUBMIT` has job ID 0. `SUBMIT_REPLY` also has header job ID 0; its payload names
the allocated entity. `entity_kind` is JOB=1 or CRON=2. Job state is the initial
QUEUED/WAITING value; cron state is 0. Send the reply and END before scheduling
or emitting events for the newly accepted job. `NODES` returns zero or more
NODE_ITEM frames then END. `STATUS` returns STATUS_REPLY then END. Absent worker
or checkpoint IDs/sizes use 0; checkpoint age is UINT64_MAX when absent.

`EVENT.event_kind` is JOB_STATE=1, WORKER_STATE=2, CRON_INJECTED=3. State fields
use the corresponding enum, or 0 when not applicable. Unused IDs are 0. For a
worker event the header job ID is 0. Events are unsolicited and never terminate
a command response. Output is delivered only through logs, never EVENT.

### Milestone 2

| ID | Name | Direction | Payload |
| --- | --- | --- | --- |
| 30 | HEARTBEAT | W to S | worker_id:u32, cpu_load:u32, running_jobs:u32 |
| 31 | JOB_TELEMETRY | W to S | telemetry:Telemetry |
| 32 | CANCEL | C to S | empty |
| 33 | LOG_UNFOLLOW | C to S | follow_request_id:u32 |
| 34 | JOB_STOP | S to W | reason:StopReason |
| 35 | WORKER_DRAIN | W to S | worker_id:u32 |
| 36 | WORKER_DRAINED | W to S | worker_id:u32 |
| 37 | LOG_CAUGHT_UP | S to C | next_stream_offset:u64 |

Send HEARTBEAT every 2 seconds, followed by one JOB_TELEMETRY frame per running
job on the same connection. The header identifies each telemetry run. Heartbeat
arrival refreshes liveness immediately; do not wait for telemetry to finish.
Before Milestone 2, `LOGS.follow=true` returns UNSUPPORTED. Implementing worker
output updates in Milestone 1 is still mandatory.

### Milestones 3 and 4

| ID | Name | Direction | Payload |
| --- | --- | --- | --- |
| 40 | CRONS | C to S | empty |
| 41 | CRON_ITEM | S to C | template_id:u32, owner:str, spec:JobSpec, runs:u64, active:bool |
| 42 | CANCEL_CRON | C to S | template_id:u32 |
| 43 | CACHE_BEGIN | W to S | generation:u32 |
| 44 | CACHE_ITEM | W to S | generation:u32, sha256:str, basename:str, size:u64 |
| 45 | CACHE_END | W to S | generation:u32 |
| 46 | CACHE_MISS | W to S | sha256:str |
| 50 | MIGRATE | C to S | empty |
| 51 | CHECKPOINT | W to S | source_sequence:u64, data:bytes |

CRONS returns CRON_ITEM frames then END. CANCEL_CRON uses header job ID 0;
MIGRATE uses the target job ID. Successful CANCEL, CANCEL_CRON, MIGRATE and
LOG_UNFOLLOW return END only. END acknowledges the request, not completion of an
asynchronous stop. Actual state changes arrive as EVENT frames.

Cache frames are unsolicited, use job/run IDs 0, and form a complete inventory
snapshot. Generations increase per worker connection. Apply a new inventory
atomically only at CACHE_END, retaining the previous complete snapshot until
then. Send a snapshot after registration and after additions or evictions.

## 8. Submission and execution sequence

1. Validate the entire SUBMIT request and queue admission before allocating an
   accepted job. Owner comes from the connection, never from client payload.
   Resolve relative executable paths against the scheduler's working directory.
2. Accept QUEUED jobs, or WAITING jobs with unresolved dependencies. Retain job
   records after they leave the queue so status and logs still work.
3. Select a worker, atomically reserve its cores/RAM/GPUs, and allocate a new
   run ID. Keep a private `dispatch_pending` flag on the QUEUED job so another
   scheduling pass cannot assign it again. This is not a new public job state.
4. Send JOB_PREPARE. `execution_mode` is PATH=1, TRANSFER=2, CACHED=3.
   `executable_name` is the original command for PATH, or a basename for a file.
   `sha256` is empty for PATH, optional for Milestone 1 TRANSFER, and required
   for Milestone 3 file executions. A hash is exactly 64 lowercase hex bytes.
5. If a saved checkpoint exists, send it first with file kind CHECKPOINT=2.
   Then send the executable with file kind EXECUTABLE=1 for TRANSFER. PATH
   requires no executable bytes. CACHED requires a local verified cache entry.
   JOB_PREPARE is metadata only. When checkpoint_number is nonzero, the worker
   must finish receiving that checkpoint before resolving the cached reference
   or reporting JOB_READY/CACHE_MISS.
6. Worker validates/prepares all required files and returns JOB_READY. Scheduler
   sends JOB_START only if the reservation is still valid and no stop is pending.
7. Worker starts the child and sends JOB_STARTED before JOB_OUTPUT or JOB_FINISHED.
   Scheduler changes QUEUED to RUNNING on JOB_STARTED and notifies the owner.
8. Worker sends output chunks, then final output, then exactly one JOB_FINISHED.
   Scheduler applies the result once, releases the reservation once, sends owner
   events, and wakes scheduling. Worker removes the job directory after handing
   final output and result to its connection writer.

The worker must never execute before JOB_START. Reject malformed, duplicate, or
out-of-order execution commands. A duplicate JOB_START must not fork twice.
Before JOB_STARTED, preparation or fork failure uses JOB_REJECTED. After it,
setup/exec failure uses JOB_FINISHED with START_ERROR and an explanatory detail;
command-not-found uses exit code 127. A pre-start rejection releases resources
and normally marks the job FAILED. Draining races instead return BAD_STATE and
requeue it without consuming an attempt.

For each file send FILE_BEGIN, contiguous FILE_CHUNK frames starting at offset
0, then FILE_END. One file per run is in transit at a time; different runs may
interleave. Validate kind, offset, declared total size, and hash when supplied.
Write to a temporary file and publish only a complete transfer. Reject basename
values containing `/`, `\\`, `..`, or an empty name. Never use a transmitted name
as an unrestricted destination path. Set executable permissions before running.
Executables have no protocol-wide total-size limit; stream them with bounded
memory. Checkpoints have the limit in section 2.

Use a PATH command when no scheduler-readable file can be obtained. Log this
choice clearly; if the worker cannot execute it, return a normal start failure.
Transfer a readable executable as a file. Never call `system()` or interpret the
command as shell syntax. v1 supports one executable and no user-supplied argv.

Reserve resources throughout preparation and execution. Node free counts subtract
both pending and running reservations. On stop, do not reuse resources until the
worker confirms cleanup or is declared DEAD. On JOB_REJECTED/JOB_FINISHED, accept
only the active worker connection and run ID. Late reports are ignored and logged.

## 9. Logs, responses, and slow connections

Worker JOB_OUTPUT offsets count bytes in that run's merged stdout/stderr file,
starting at zero. Send new bytes roughly every 500 ms and flush the remainder
before JOB_FINISHED. Scheduler expects contiguous offsets per run and appends
accepted bytes to one job-wide log stream that continues across retries.
Client LOG_DATA offsets refer to this job-wide stream, not the worker file.

Retain at least the last 64 KB per job. A plain LOGS request returns the retained
snapshot in LOG_DATA frames then END. Its first offset may be greater than zero;
the client should indicate that earlier output was truncated.

For follow mode, atomically capture the retained snapshot and register a subscriber
at the snapshot's ending offset. Send snapshot data, LOG_CAUGHT_UP, then newly
arriving data without gaps or duplicates. The original LOGS request remains open
until the job becomes terminal or the user unfollows. A terminal job returns its
snapshot then END immediately. On normal follow completion, END.detail supplies
a short finished footer. Follow continues across worker failures and requeues.

Ctrl+C during follow sends LOG_UNFOLLOW with a fresh request ID and the original
follow request ID in its payload. Stop forwarding that subscription, END the old
LOGS request, and END the LOG_UNFOLLOW request. Do not exit the client. All follow
LOG_DATA frames carry the original LOGS request ID. Multiple requests and owner
events may interleave on a connection, but frame bytes must never interleave.

Keep per-connection queues bounded. Disconnect a client that cannot consume
responses/events instead of blocking scheduler work or growing memory forever.
On a persistently stalled worker connection, fail the connection and use worker
failure recovery. Do not silently discard output while keeping a run successful.
Chunk file transfers so heartbeats and control messages can be serviced between
chunks; never hold the writer for an entire executable transfer.

## 10. Scheduler state and scheduling rules

- Wake scheduling on submit, completion, worker availability, and heartbeat, and
  at least once each second. Copy query snapshots under brief locks; perform
  sorting, transfer, and response writing outside those locks. Revalidate and
  reserve atomically before dispatch.
- FIFO sorts by original submission sequence and stops at the first unmatched
  job. PRIORITY uses `max(1, priority - floor(queued_wait_ms / 10000))`, then
  original submission sequence. PRIORITY continues past an unmatched job.
  Track cumulative queued waiting across requeues, excluding running and WAITING
  time. Only QUEUED jobs without dispatch reservations enter a pass.
- New submissions fail immediately with QUEUE_FULL when the 1024-entry admission
  queue is full. Maintain a separate internal recovery backlog for already
  accepted requeues that cannot fit. Merge both sources when ordering eligible
  jobs. Never discard an accepted job because recovery finds the queue full.
  Block new admission while the recovery backlog is nonempty.
- Only ALIVE workers with sufficient free cores, RAM, and GPUs qualify. Milestone
  1 breaks worker ties by lowest worker ID. Milestone 2 compares CPU load then
  running-job count. Milestone 3 compares idle status, cached executable, CPU
  load, then running-job count. Lowest ID breaks any remaining tie. Unknown load
  sorts after measured load. Pending reservations count as busy for selection.
- The complete later pass order is preempted-job hold, quota check, resource
  match/migration exclusion, worker selection, preemption if needed, then FIFO
  stop or PRIORITY continue. Preserve the order on page 18 of the brief.

## 11. Failure, cancellation, and draining

With RESILIENCE enabled, one missed two-second heartbeat interval marks a worker
SUSPECTED, and three consecutive misses mark it DEAD. A heartbeat restores a
SUSPECTED worker to ALIVE. Remember draining intent independently so recovery
cannot make a draining worker eligible again. A known broken TCP connection may
mark a worker DEAD immediately. Log and notify owners of affected running jobs.

On DEAD, release each reservation exactly once. RUNNING jobs become LOST and then
QUEUED for the next attempt, or FAILED_PERMANENT after attempt 3. Pending jobs
that never started are requeued without consuming an attempt. A worker-side lost
connection must kill and reap its children before re-registering.

CANCEL on QUEUED/WAITING jobs removes them from eligibility and marks CANCELLED.
For a pending or running assignment, send JOB_STOP with CANCEL and wait for
cleanup before freeing resources. JOB_STOP must also abort an unstarted prepared
run. Such a run replies JOB_FINISHED with CANCELLED even without JOB_STARTED;
this is the exception to the normal started-before-finished sequence.
Cancelling a terminal job returns BAD_STATE. Repeated stops must not double-free
resources. Serialize competing finish/stop decisions per run. A worker that has
already completed a run keeps its original result. If connection failure makes
a stop outcome uncertain, cancellation still prevents a retry.

Local worker `drain` or SIGINT sends WORKER_DRAIN and refuses new preparations.
Continue heartbeats and finish current jobs. When no running or prepared runs
remain, send WORKER_DRAINED, flush the connection, and exit. Only accept
WORKER_DRAINED for a DRAINING worker with no assignments; normal departure does
not rescue jobs. There is no client drain command.

JOB_FINISHED EXITED with code 0 means COMPLETED. Other ordinary execution failures
mean FAILED. Preserve known timeout/memory/stop reasons instead of replacing
them with a bare SIGKILL diagnosis. Signal numbers are native OS numbers;
detail must include a readable signal name because numbers can differ by OS.
Unused exit_code or signal_number fields are zero. PREEMPTED/MIGRATED results
requeue the job without increasing attempt.

## 12. Later scheduling features

Dependencies resolve JOB/GROUP references at submission. Reject unknown IDs.
Expand a group into its current job IDs once, remove duplicates, and store those
IDs. WAITING jobs become QUEUED only after all dependencies complete. A failed,
permanently failed, or cancelled dependency cascades CANCELLED to dependents.

Preemption applies when a priority-1 job has no qualifying worker. Use effective
priority for this decision. Select only preemptible victims on one worker, lowest
priority first and newest first within a priority. Kill just enough to meet the
request. Wait for victim stop results before allocating their resources. Hold
victims until the beneficiary starts, or release the hold if it fails/cancels.

Quota checks count each user's RUNNING cores. More than 50% of all non-DEAD
worker cores is over quota only when another user with a QUEUED job is not over
quota. Exactly 50% is allowed. The quota skip overrides FIFO blocking and job
priority. Log each skip once per affected job pair per scheduling pass.

`every_sec > 0` creates a template, not a normal job, and cannot combine with
dependencies. The first child is due immediately, followed by one every interval
even if earlier children still run. Each child receives its own job ID and an
owner CRON_INJECTED event, with template ID and run number in detail. If the
admission queue is full, retain due occurrences in an internal pending list and
admit them as capacity becomes available; never silently lose a due run. A
template whose first occurrence cannot be admitted is still registered, with
that occurrence pending. CANCEL_CRON stops future and unadmitted occurrences;
already accepted queued/running children remain. Return NOT_FOUND for unknown
templates and BAD_STATE for already cancelled templates.

## 13. Executable caching and checkpoints

Milestone 3 uses SHA-256 content hashes and a worker-local cache directory,
default `cache/`, overridden by OSN_CACHE_DIR. Workers on the same machine must
use separate cache directories. Cache size defaults to 512 MB and is overridden
by OSN_CACHE_SIZE_MB. Evict least recently used entries, with use recorded when
a job starts. Pin programs used by prepared/running jobs and the just-received
program. If protected entries exceed the limit, temporarily exceed it and log
why rather than deleting them.

For CACHED preparation, a missing file triggers CACHE_MISS, not JOB_REJECTED.
Scheduler removes that hash from its worker inventory and sends a normal
executable transfer for the same run and reservation. No attempt is consumed.
Worker then returns JOB_READY. On reconnect, scan and verify the cache before
advertising its inventory. Always copy the executable into the job directory
before execution so job cleanup and cache eviction remain separate.

Milestone 4 sets OSN_CHECKPOINT_DIR to the job's writable checkpoint directory.
The job atomically publishes `state` there. Worker polls changes about every
500 ms and sends CHECKPOINT with a per-run increasing source sequence. Scheduler
accepts only active-run updates, increments its job-wide checkpoint number, and
keeps the latest bytes and receive time. Job-wide numbers survive reassignments.
JOB_PREPARE names the saved checkpoint number; restore its bytes before sending
the executable or cache reference and before JOB_START.

MIGRATE requires a RUNNING job and owner identity. JOB_STOP MIGRATE sends SIGTERM,
captures a final checkpoint if produced, and sends SIGKILL after two seconds if
needed. Send final checkpoint/output before the MIGRATED result. Requeue without
an attempt charge. Exclude the previous worker if any other worker qualifies.
Preemption uses immediate SIGKILL and the latest previously received checkpoint.
No checkpoint means restart from the beginning. Reject oversized checkpoints
with a logged warning and retain the previous valid checkpoint; do not corrupt
the stream or fail the job solely because a checkpoint was too large.

## 14. Shared implementation interfaces

Put message IDs, enum values, constants, host-side records, and function
declarations under `include/common/`; implementations belong in `src/common/`.
Use one encoder/decoder shared by all three executables. Header encoding and
connection I/O are implemented in `src/common/protocol.c` and
`src/common/connection.c`. See `include/common/connection.h` for socket ownership,
callback lifetime, return values, queue limits, and polling requirements.

```c
/* Implemented entry points declared in the shared headers. */
int protocol_encode_header(const struct frame_header *header,
                           uint8_t output[24]);
int protocol_decode_header(const uint8_t input[24],
                           struct frame_header *header);
/* Incremental receiver and queued writer must retain partial-frame progress. */
int connection_receive(struct connection *connection);
int connection_queue_frame(struct connection *connection,
                           const struct frame_header *header,
                           const uint8_t *payload);
int connection_flush(struct connection *connection);
/* Queueing copies the payload. Flush retains unsent bytes on EAGAIN. */
/* Receive returns complete decoded frames through the connection dispatcher. */
```

Header encode/decode and queue_frame return 0 on success and -1 on invalid input
or allocation failure. receive/flush use 0 for no progress because of EAGAIN,
1 for progress, -1 for orderly EOF, and -2 for a fatal error. Fatal errors close
the connection; EOF with an incomplete frame is fatal. receive dispatches every
complete frame it obtained before returning. The dispatcher may borrow payload
storage only during its callback and must copy data it retains. A successful
flush result may still leave queued bytes, so keep monitoring writability.
The caller owns connection lifetime; no transport function frees it implicitly.
Keep scheduler policy and worker process management out of the shared transport.
Route scheduler and worker state/decision logs through one thread-safe
`log_line()` implementation with `[HH:MM:SS]` timestamps.

## 15. First implementation checks

1. Build all three executables with one Makefile on Linux and macOS.
2. Verify a header encoder against this empty NODES request, request ID 1:

   ```text
   4f 53 4e 43 00 01 00 0c 00 00 00 00
   00 00 00 01 00 00 00 00 00 00 00 00
   ```

3. Split a frame across writes and combine two frames into one write; decode both
   correctly. Reject malformed headers, lengths, and truncated payloads.
4. Register one worker, log in one client, and return nodes/status replies.
5. Submit a PATH command, then a transferred executable. Confirm output, result,
   and resource release. Run two jobs concurrently without mixing their output.
6. Confirm FIFO head blocking, PRIORITY aging, queue-full rejection, and events
   arriving while the client is idle. Later add stale-run, reconnect, cache-miss,
   follow/unfollow, and stop/finish race checks as their milestones are built.
