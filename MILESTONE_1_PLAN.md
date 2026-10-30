# Milestone 1 work and commit plan

Use A, B and C as placeholders for the three teammates. This is a proposed implementation plan based on the course PDF, not extra course requirements. The folder currently contains the PDF and a short README, with no implementation or `.git` directory. The commits below are planned commits for each person to make as they implement their own work.

## What you are building

You are making three C programs. A user types a command into `client`. The `scheduler` queues the job, chooses a worker with enough free resources and sends it the executable. The `worker` starts the executable as a child process, applies limits and sends its output and result back. The scheduler updates the job and notifies its submitting client.

Milestone 1 explicitly covers Sections 2, 3, 4A-B, 5A-B and 7. Those sections total 155 marks.

The three programs must support these command lines:

```text
./scheduler <port> <FIFO|PRIORITY> [-discovery-port <port>]
./worker [<scheduler_ip> <scheduler_port>] -cores <num> -ram <MB> [-gpu <num>] [-discovery-port <port>]
./client [<scheduler_ip> <scheduler_port>] [-user <username>] [-discovery-port <port>]
```

The default discovery port is 9999. Workers and clients try UDP discovery first, even when a fallback address was supplied.

## Who owns what

| Person | Main responsibility | PDF coverage | Why this is a substantial share |
| --- | --- | --- | --- |
| A | Scheduler, job queue, resource accounting and binary dispatch | 2A, 4A-B; scheduler handling for Section 3 | Concurrent clients/workers, queue correctness, FIFO/priority aging, dispatch and query handling |
| B | Worker registration, process execution, limits and output collection | 2B, 5A-B | Process lifecycle, privilege dropping, Linux/macOS limits, concurrent jobs and transferring output |
| C | Client, shared protocol/transport, UDP discovery and shared logger | 2C, 3, 7 | Interactive client, asynchronous events, reconnects, reusable networking, message encoding and discovery |

Sections 2A and 4A-B are 60 marks, 2B and 5A-B are 50, and 2C, 3 and 7 are 45. These are coverage references, not individual grades or exact measures of effort. C has cross-program networking work, and A implements the server side of C's commands. Each person also owns their module's checks and integration fixes. Rebalance if shared networking takes longer than expected.

Do not give one person only documentation or tests. All three have working C code to write and demonstrate.

## File ownership

This is a suggested layout. Keep shared definitions in one place.

```text
include/common/            C: shared types and public interfaces
src/common/                C: protocol, TCP helpers, UDP discovery, log_line
src/client/                C: command parser, prompt, replies and events
src/scheduler/             A: main, connection handlers, tables, queue,
                             scheduling, executable transfer, log storage
src/worker/                B: main, registration, executable reception,
                             runner, limits, output collection, cleanup
tests/scheduler/           A: queue tests and fake client/worker
tests/worker/              B: test programs and fake scheduler
tests/client/              C: parser checks and fake scheduler
tests/common/              C: framing and discovery checks
Makefile                  C: top-level targets and shared flags
src/scheduler/sources.mk   A: scheduler source list
src/worker/sources.mk      B: worker source list
src/client/sources.mk      C: client source list
```

C's Makefile can optionally include the three source lists, so an unfinished program does not prevent another target from building. Each person edits their own source list. This avoids three people repeatedly editing the same Makefile.

## Agree on interfaces before implementation

The C1 declarations now exist in `include/common/`, with exact wire layouts and usage rules in [docs/PROTOCOL.md](docs/PROTOCOL.md). Use those files as the shared contract rather than independently recreating the proposals below. Everyone can start local code against these declarations immediately; the actual shared implementation is needed later for linked builds and integration. No Git commit has been created in this copy.

The following are proposed design choices. The PDF lets you design your own wire protocol.

- Use length-prefixed TCP frames with a message type and request ID. Encode fields explicitly with fixed-width integers and network byte order. Do not send raw C structs with compiler padding, and do not assume one `recv()` equals one message.
- Keep text lengths explicit and executable/output data binary-safe. Put a bounded size on each frame and transfer files in chunks. Define limits and error replies in the shared header.
- Use common job fields for executable name/path, execution mode, cores, RAM in MB, time in seconds, priority 1-3 and GPU count. The scheduler derives the owner from the logged-in connection. Each job has a scheduler-assigned ID and stable submission sequence.
- Define all lifecycle enums now: job states `QUEUED`, `RUNNING`, `COMPLETED`, `FAILED`, `CANCELLED`, `WAITING`, `LOST`, `FAILED_PERMANENT`; worker states `ALIVE`, `SUSPECTED`, `DEAD`, `DRAINING`. Implement only the milestone's transitions now.
- Treat a bare PATH command such as `ls` differently from a transferred executable. A slash-containing path refers to a file the scheduler reads and transfers. Report an unreadable file clearly. Copy a transferred file into the job directory and make it executable. B runs the local copy, never the scheduler's original filesystem path.
- A proposed `discover_scheduler()` returns an address after up to three attempts, each waiting at most two seconds. It uses the supplied fallback only after discovery fails, and returns an error if neither succeeds. B and C call this same helper at startup and after disconnection. A runs C's discovery responder.
- A proposed `log_line(component, worker_id, format, ...)` writes one complete timestamped line atomically. Before registration B logs without an ID. Afterwards B supplies its assigned ID. Each module decides when to log; C owns the formatter.
- A proposed transport layer provides complete-frame send/receive, disconnect/error handling and codecs usable by fake peers. A owns scheduler threading and per-connection write serialization; B owns worker write serialization. No two threads may interleave bytes on one socket.

Agree on these message families and fields in C1:

| Message family | Direction | Required content |
| --- | --- | --- |
| `LOGIN`, `LOGIN_REPLY` | Client to scheduler and back | Username as the client's first application message; accepted/error result |
| `REGISTER`, `REGISTER_REPLY` | Worker to scheduler and back | Offered cores/RAM/GPUs; assigned worker ID |
| `SUBMIT`, `SUBMIT_REPLY` | Client to scheduler and back | Job specification; job ID or explicit error such as `QUEUE_FULL` |
| `NODES`, `NODES_REPLY` | Client to scheduler and back | Worker IDs, states, free/total resources and running-job counts |
| `STATUS`, `STATUS_REPLY` | Client to scheduler and back | Job ID; state, owner and assigned worker or none |
| `LOGS`, `LOGS_REPLY` | Client to scheduler and back | Job ID; output collected so far, possibly across multiple frames |
| `ASSIGN`, `BINARY_CHUNK`, `BINARY_END` | Scheduler to worker | Job ID/specification, execution mode, file length and ordered file bytes |
| `JOB_STARTED`, `OUTPUT`, `JOB_DONE` | Worker to scheduler | Job ID; ordered output chunks; exit/result and known limit/setup failure reason |
| `EVENT` | Scheduler to submitting client | Job ID, new state and useful reason/worker information |
| `ERROR` | Either direction as appropriate | Request ID when applicable and a clear error code/message |

For file jobs, B waits for the full executable before starting. For PATH commands, `ASSIGN` can contain everything needed. A reserves resources once when dispatching, and B acknowledges actual startup with `JOB_STARTED`. A handles assignment/setup failure by releasing reservations and reporting failure; it must not leave resources busy forever. Completion must release resources exactly once. Output frames must precede `JOB_DONE` so logs are complete when completion is announced.

## A's commits: scheduler

Use branch `milestone1/scheduler`. These are suggested commit messages and boundaries, not commands to create empty commits.

| Commit | Commit message | Code included | Dependencies |
| --- | --- | --- | --- |
| A1 | `Add scheduler startup and concurrent connection handling` | Exact CLI, TCP listener, worker/client connection handlers, thread lifecycle and scheduler source list | C1 interfaces; C2/C3 implementations for a real network run |
| A2 | `Add worker registration and client query handlers` | Unique worker IDs, capacity/free-resource tables, username/session storage, `nodes`, `status`, job IDs and ownership | A1; fake peers suffice |
| A3 | `Add bounded job queue and resource accounting` | Validated submit, 1024-entry pending queue, immediate `QUEUE_FULL`, atomic reservation/release, synchronized tables | A2; independently check with synthetic jobs |
| A4 | `Implement FIFO scheduling and priority aging` | Strict FIFO head blocking; priority order and 10-second aging; matching ALIVE workers; wakeups and at least one pass per second | A3; no real worker needed for algorithm tests |
| A5 | `Dispatch executable files and process worker job results` | Scheduler-side file reading and chunked transfer, PATH mode, startup/result handling, output storage, `logs`, owner-only events | A4 and C2; fake worker initially, B5 for integration |
| A6 | `Verify scheduler concurrency and integrate the core pipeline` | Focused scheduler checks, startup/decision/state logs, responsiveness during transfer, queue-full checks, fixes from real client/worker runs | C3 for real discovery/logger, B5 and C5 for end-to-end checks |

A owns the server side of client commands. C does not have to edit scheduler internals to make a command work.

Choose the PDF's thread-per-connection model for this plan, with a separate scheduling thread, mutexes and condition variables. Do not hold shared table/queue locks while reading files, sending TCP data or waiting for a child. Send dispatch data through per-worker outbound work so a slow transfer does not stop scheduling on other workers. Queries take brief snapshots and format/send outside the lock.

FIFO stops at the first job that cannot fit. PRIORITY tries the next job if one cannot fit. Compute effective priority as `max(1, original_priority - floor(queued_seconds / 10))`, with older submission first on ties. A deterministic eligible worker choice, such as lowest worker ID, is a proposed M1 policy; load-aware selection is in M2.

Keep completed job records separate from the 1024-entry pending queue. The PDF also requires future internal requeues never to be dropped when that queue is full; leave a documented internal-requeue path rather than reusing a submit rejection path when M2 adds retries.

## B's commits: worker

Use branch `milestone1/worker`.

| Commit | Commit message | Code included | Dependencies |
| --- | --- | --- | --- |
| B1 | `Add worker startup and capacity validation` | Exact CLI, real core/RAM detection and clamping, unclamped GPU counter, non-root warning, worker source list | Can start alone; C1 for shared types |
| B2 | `Add isolated job execution with fork and exec` | Local runner, private job directory, executable preparation, stdout/stderr redirection, `fork`, `execvp`, reaping | C1 job types; local test programs only |
| B3 | `Enforce worker privilege and resource limits` | Ordered chown/drop-privilege/rlimit setup, checked failures, CPU and wall-clock timeouts, macOS resident-memory checks | B2; no scheduler needed |
| B4 | `Collect incremental job output and final results` | Roughly 500 ms output reads, final flush, completion/failure result, multiple active-job monitoring and cleanup | B2/B3; callback collecting output locally is enough |
| B5 | `Connect worker registration and binary reception` | Discovery-first startup, registration/ID, assignment and executable reception, runner wiring, output/result frames, disconnect cleanup and re-registration | B1-B4, C2 and C3; fake scheduler initially, A5 for integration |
| B6 | `Verify worker execution on Linux and macOS` | Limit/output fixture programs, focused checks, timestamped worker state logs and fixes from real scheduler runs | B5; A5 for integration; both OS environments for portability claims |

B's runner should accept a job specification plus callbacks for output and final result. Initially those callbacks can write into a local test collector. B can finish B2-B4 while A and C are still working.

Implementation details that belong to B:

- Create `/tmp/osn_job_<JobID>`, prepare the executable, enter the directory and arrange its ownership. When root, resolve `nobody` with `getpwnam()`, then call `setgid()` before `setuid()`. Fail the job if privilege switching fails. As the PDF specifies, non-root runs warn and skip chown/privilege switching.
- Apply `RLIMIT_CPU`, `RLIMIT_AS` with MB-to-bytes conversion and `RLIMIT_NPROC`, in the PDF's order. All jobs using `nobody` share its process budget. Also enforce a real elapsed-time deadline and kill overdue jobs. Report the prescribed wall-clock timeout reason for the CPU-timeout case too.
- On macOS, `RLIMIT_AS` is ineffective, so periodically inspect resident memory with `proc_pidinfo()` and kill over-limit jobs. This enforcement is M1 even though the status telemetry display is M2.
- Redirect both stdout and stderr to `output.log`. Send new bytes about every 500 ms while the process runs, flush remaining bytes before the final result and clean up afterwards. Child output must not leak into the worker terminal.
- Monitor concurrent assigned jobs without blocking the network receive path behind one `waitpid()`. On scheduler disconnection, kill and reap active jobs, rediscover and register for a new worker ID. Do not add heartbeat-based retry/recovery in M1.

## C's commits: client and shared code

Use branch `milestone1/client-common`.

| Commit | Commit message | Code included | Dependencies |
| --- | --- | --- | --- |
| C1 | `Define shared job types and message interfaces` | Shared headers, protocol note, message fields, units, enums, limits, error codes and function declarations | Short agreement with A/B; no implementation prerequisite |
| C2 | `Implement framed TCP transport and message codecs` | Partial reads/writes, multiple frames per read, binary-safe chunks, bounded frame validation, shared TCP helpers, initial Makefile | C1; socket-pair/fake-peer checks suffice |
| C3 | `Add UDP discovery and timestamped shared logging` | Three timed discovery attempts, fallback helper, configurable port, restart-friendly responder using reuse options, thread-safe `log_line` | C1; C2 where connection helpers are reused |
| C4 | `Add client login and validated command parsing` | Exact CLI, username validation, `-user`, prompt, submit flag checks, `nodes`/`status`/plain `logs`, client source list | C1/C2; fake scheduler suffices |
| C5 | `Handle client events and reconnect authentication` | Socket plus stdin handling, idle-prompt events, reply/event distinction, discovery/reconnect and automatic username resend | C3/C4; fake scheduler initially, A5 for integration |
| C6 | `Verify discovery and client integration across machines` | Client/common checks, split/coalesced-frame checks, duplicate-name login, discovery timeout/fallback/restart checks and integration fixes | C5, A5, B5 for the complete pipeline |

Usernames contain 1-31 letters, digits, `_`, `-` or `.`. Support multiple clients with the same username. Agree in C1 that events route to the submitting session; sharing a username does not automatically subscribe another session to its jobs. For M1, do not promise replay of events missed during a disconnect. Re-authentication itself is required.

The client can use `poll()` on stdin and the scheduler socket, or a dedicated reader thread. In either case, messages must appear while the user is idle at `client>`. Include the submitting user in scheduler logs about client actions.

## What actually depends on another person's code

| Work | Can proceed independently? | When a teammate's code is needed |
| --- | --- | --- |
| A's queue, accounting and scheduling algorithms | Yes, using in-memory jobs and workers | C1 types; C2 transport for real connections |
| B's runner, limits and output collection | Yes, using a local runner driver and test executables | C1 job specification; C2/C3 and A5 for network integration |
| C's client and common networking | Yes, using socket pairs and a fake scheduler | A's real handlers for final compatibility checks |
| Discovery in worker/client | Call a declared helper while it is unfinished | C3 implements the helper; A starts its responder |
| Executable transfer | A writes the sender; B writes the receiver against C1's agreed fields | C2 handles framing; A5 and B5 must meet for a real transfer |
| Events and logs | A produces replies/events; B produces output; C displays them | A5, B5 and C5 for the complete flow |

There is no need for B to wait for the scheduler before writing the runner, or for C to wait for either executable before writing the client. The main early prerequisite is the shared contract. Working shared networking is a build/integration prerequisite, not a prerequisite for every module's logic.

## Integration order and commits

1. Agree on interfaces and land C1 first. Use the same declarations on all three branches.
2. Work concurrently. A writes tables/queues/policies, B writes the local runner/limits, and C writes transport/discovery/client. Keep fake peers under each person's own test directory.
3. Bring C2/C3 into A and B's branches when ready. First connect a worker and get an ID, then log in a client and run `nodes`.
4. Connect A5 and B5. Submit a bare PATH command, then a small transferred executable built for the worker's OS/architecture. The system transfers binaries; cross-compiling arbitrary binaries is not a course requirement.
5. Connect C5 and verify events, output retrieval, concurrent submissions and resource release. Each person commits fixes to the module they own. A handles scheduler integration, B worker integration, C client/common integration.
6. Merge the actual authored commits into the team's real Git repository. Preserve author history when merging; avoid squashing everyone into one person's commit. Each teammate uses their own Git identity and commits their own code.

Six commits per person is a suggested breakdown, not proof of equal contribution. Make smaller commits when a real change warrants one. Review each other's code and have each person demonstrate their own module. This plan does not initialize Git, create commits, impersonate teammates or implement M1.

## Milestone boundaries that are easy to miss

- **Included now:** UDP discovery and reconnect discovery, username login, `nodes`, submit flags, `status`, plain `logs`, asynchronous job events, 1024-entry bounded queue, FIFO/PRIORITY with aging, resource matching, executable transfer, actual worker limits, incremental worker-to-scheduler output and centralized timestamped logging.
- **Later:** heartbeat failure detection/retries, load-aware selection, cancellation, status telemetry, full signal diagnosis, client `logs -f`, worker draining, dependencies, preemption, quotas, cron, caching and checkpoints/migration. Keep their lifecycle states in shared types now, as the PDF asks.
- Section 3 lists `cancel` and `logs -f`, but explicitly links them to M2 Sections 5C and 6. The proposed M1 client reports them as unavailable rather than pretending they work. This follows the explicit milestone list; confirm only if course staff give a different interpretation.
- Section 5B explicitly requires output forwarding about every 500 ms in M1. Defer the client's follow mode, not the worker's incremental capture. A stores output so plain `logs` can show what has arrived; retaining at least the last 64 KB early is a useful proposed choice from M2, not a separate stated M1 minimum.
- Transfer of an executable is required for the basic pipeline by the introduction. Section 13 adds caching later; it does not defer ordinary file transfer.

## Acceptance checks and owners

| Check | Expected result | Lead |
| --- | --- | --- |
| Start scheduler, two workers and two clients | Unique worker IDs; correct capacity/free resources; both clients can query | A |
| Connect two clients using the same username | Both remain usable; events follow the submitting-session rule | C with A |
| Run without fallback on a hotspot; then block discovery with fallback set | Discovery succeeds normally; fallback is used only after all three attempts fail | C |
| Restart scheduler immediately | Discovery responder rebinds; worker/client rediscover and re-register/re-authenticate | C with A/B |
| Submit a tiny transferred executable and a bare PATH command | Both run; path job's file reaches a separate worker machine | A with B |
| Use idle prompt while a job starts/finishes | The submitting client sees state events without pressing Enter | C with A |
| Submit jobs whose combined resources exceed capacity | Extra jobs wait; no negative free resources; resources return once on completion | A |
| Put an oversized job ahead of a small one | FIFO blocks the small job; PRIORITY can try the next eligible job | A |
| Wait 10 and 20 seconds with a priority-3 job queued | Effective priority becomes 2, then 1; older-first tie ordering holds | A |
| Keep workers unavailable and submit 1025 queued jobs | First 1024 accepted; next returns `QUEUE_FULL` promptly | A |
| Ask for `nodes`/`status` during scheduling and a large transfer | Queries stay responsive; dispatcher/file I/O does not hold shared locks | A |
| Run an output-producing job | stdout and stderr arrive incrementally at scheduler; plain `logs` shows collected bytes; final bytes arrive before completion | B with A/C |
| Run a sleeping job and a CPU-bound job past `-time` | Both fail with the required timeout reason; processes are reaped | B |
| Exceed memory on Linux and macOS | Job fails under the specified address-space/resident-memory enforcement | B |
| Run as root and attempt a write into a protected user directory | Job executes as the dummy user and the write fails; privilege-switch failure prevents execution | B |
| Run worker without sudo | Clear warning; job still runs under the PDF's non-root behavior | B |
| Finish a job and inspect worker output/temp files | No raw child output in terminal; final report sent; job directory cleaned | B |
| Inspect scheduler and worker logs | `[HH:MM:SS]`, correct component/worker ID, state/decision/discovery logs, usernames for client actions | C formatter; A/B call sites |

Run the relevant checks as each feature lands. A fake peer checks local behavior; it does not replace the final three-program test or Linux/macOS checks. No implementation checks have been run yet because this folder has no implementation.

Source: `Course_Project_Clean.pdf`, especially pages 2-12 and the marks summary on page 19. Page 3 gives the milestone scope, pages 4-8 define startup/client/scheduling, pages 9-11 define execution/limits/output, and page 12 defines logging.
