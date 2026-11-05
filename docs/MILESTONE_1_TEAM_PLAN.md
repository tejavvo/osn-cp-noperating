# Milestone 1 team plan

The goal is to submit a job from the client, have the scheduler choose a worker,
run the job with limits, and get its output and final status back.

Use the [project brief](../Course_Project_Clean.pdf), especially Sections 2, 3,
4A-B, 5A-B and 7. Use [PROTOCOL.md](PROTOCOL.md) for message IDs, field order,
encoding, and connection flows. Proposed filenames below are implementation
suggestions; the ownership and required behavior are what matter.

## Who owns what

| Member | Person | Main responsibility |
| --- | --- | --- |
| A | Tushita | Scheduler, job queue, scheduling, resource accounting, scheduler request handling |
| B | Teja | Worker, running jobs, limits, executable reception, output capture and cleanup |
| C | Me | Client, shared transport and payload codecs, UDP discovery helpers, shared logging and integration tests |

Everyone starts in parallel. Tushita and Teja can build their internal modules
before the networking helpers are finished. We integrate small working flows
as we go instead of waiting for three finished programs.

## What is already available

- The Makefile builds `scheduler`, `worker`, and `client`.
- All three entry points validate their command-line arguments and print their
  configuration. They do not yet open application connections or execute jobs.
- `include/common/types.h` defines shared records and states.
- `include/common/protocol.h` defines message IDs, constants, and the frame header.
- `src/common/protocol.c` encodes and decodes the 24-byte TCP header.
- `src/common/connection.c` handles partial frames, queued writes, backpressure,
  disconnects, and socket-local SIGPIPE suppression.
- `include/common/connection.h` documents that API and its ownership rules.
- `make test` runs the transport tests. They have passed on macOS; Linux still
  needs verification on a team member's machine.

These files are present in the working tree. Check that they have been committed
and shared before another member relies on them. Application payload codecs,
UDP discovery, the client prompt, scheduler behavior, and worker execution are
still to be implemented.

## Tushita: scheduler

Main files belong under `src/scheduler/`. Split the work into modules such as
`server.c`, `jobs.c`, `workers.c`, `queue.c`, `scheduling.c`, and `dispatch.c`.
Keep `main.c` for configuration and starting those modules.

### 1. Accept and identify connections

- Open the scheduler's TCP listening socket and accept multiple connections.
- Use the first message to distinguish CLIENT_LOGIN from WORKER_REGISTER.
- Store client usernames. Allow multiple connections with the same username.
- Assign each registered worker a unique ID, starting at 1001, and store its
  advertised total capacity. Return WELCOME followed by END.
- Connect the shared discovery responder to the scheduler's startup so clients
  and workers can find its TCP endpoint.

Use a thread per connection plus a scheduling thread, as our chosen scheduler
approach. Protect job/worker tables and queue changes with mutexes, and use a
condition variable to wake scheduling. A discovery responder can run separately.

The existing connection object is not thread-safe. Each connection thread owns
its object and performs receive/queue/flush calls. Other threads hand outbound
messages to it through a protected per-connection inbox and wake it, for example
with a pipe monitored by `poll()`. Do not call its queue function concurrently
from the scheduling thread. Never hold shared state locks during socket writes
or executable reads.

### 2. Keep the job and worker records

- A job record needs its ID, owner, command, resource request, priority,
  submission sequence, queued waiting time, state, assigned worker, attempt,
  run ID, retained output, and final result.
- A worker record needs its ID, connection, state, total resources, free
  resources, pending assignments, and running jobs.
- Keep completed job records available for `status` and `logs`.
- Store the full state enums now, while implementing only Milestone 1 transitions.
- Reserve resources atomically before dispatch. Release each reservation once
  when that run finishes or its dispatch fails. Do not dispatch a job twice.

### 3. Implement the queue and scheduling

- Keep the submission queue bounded at 1024 entries. Return QUEUE_FULL immediately
  when admission fails. Do not block the submitting connection.
- FIFO considers original submission order. If the oldest job cannot fit on any
  eligible worker, stop that pass.
- PRIORITY uses effective priority, then original submission order. Every 10
  seconds spent queued improves priority by one, down to 1. If one job cannot
  fit, continue trying later jobs.
- Require an ALIVE worker with enough free cores, RAM and GPUs. Use the lowest
  worker ID to break worker ties in Milestone 1, as specified in our protocol.
- Wake scheduling after submissions, completions and worker availability, and
  at least once per second. Status/nodes requests must remain responsive.
- Keep admission separate from internally requeued accepted jobs, following the
  recovery-backlog rule in PROTOCOL.md. Heartbeat-based rescue comes later.

### 4. Handle commands, dispatch, and results

- Handle SUBMIT, NODES, STATUS, and ordinary LOGS requests.
- Take the submitting user from the logged-in connection. Validate requests on
  the scheduler even if the client already validated them.
- Send SUBMIT_REPLY and END before a new job's state events.
- Read scheduler-local executable files and transfer them in chunks. PATH mode
  runs the command by name on the worker. Basic transfer is needed now; caching
  is a later milestone.
- Implement JOB_PREPARE, FILE_BEGIN/CHUNK/END, JOB_READY, JOB_START, and JOB_STARTED
  in the order in PROTOCOL.md. Track pending reservations before RUNNING.
- Append JOB_OUTPUT chunks to the job's retained output. Keep at least the last
  64 KB, matching the shared contract, and answer snapshot LOGS requests.
- Apply JOB_FINISHED/JOB_REJECTED once for the active worker and run ID.
- Notify the job owner's connected sessions when its state changes. Other users
  must not receive those owner events.
- Use the shared timestamped logger for state changes, scheduling decisions,
  discovery outcomes, and client actions. Include the username for client actions.

### How to work without the other programs

Start with direct calls to queue/scheduling functions and fake worker records.
Test ordering and resource accounting without sockets. Then use a small fake
client/worker or the integration tests to exercise network handlers.

### Tushita's completion checks

- Multiple clients and workers can register, and worker IDs remain unique.
- The 1025th queued submission is rejected without blocking.
- FIFO head blocking and PRIORITY aging behave differently as required.
- Two jobs cannot reserve the same free resources.
- Status/nodes still respond while a worker is receiving a large executable.
- Completion releases resources, preserves logs, and generates owner-only events.

Suggested commit sequence:

1. `Add scheduler listener and connection registration`
2. `Add job records and bounded submission queue`
3. `Implement FIFO and priority scheduling with resource accounting`
4. `Dispatch jobs and transfer executables to workers`
5. `Handle worker results and client status and log requests`

## Teja: worker

Main files belong under `src/worker/`. Suggested modules are `registration.c`,
`files.c`, `runner.c`, `limits.c`, and `output.c`.

### 1. Discover and register

- Call the shared discovery helper first, including when a fallback address was
  supplied. Use that fallback only after the discovery retries fail.
- Check actual machine cores and RAM, clamp excessive advertised values, and
  print warnings when clamping. GPU capacity is just an unclamped counter.
- Connect, send WORKER_REGISTER, and wait for WELCOME and successful END.
- Store the assigned worker ID and use it in subsequent worker logs.
- On scheduler disconnect, kill and reap active jobs before rediscovering and
  registering with a new worker ID. Heartbeat timers and retry rescue belong to
  Milestone 2, but basic disconnect behavior is part of startup/connection work.

### 2. Receive and prepare assignments

- Receive JOB_PREPARE and validate its resource request and execution mode.
- For transferred files, check contiguous offsets and total length, write to a
  temporary file, and publish only a complete executable. Set its permissions.
- Create `/tmp/osn_job_<JobID>` and copy the transferred executable into it.
- PATH commands need no executable transfer.
- Return JOB_READY only when preparation succeeds. Do not run before JOB_START.
- Track assignments by job ID and run ID. A duplicate start must never fork twice.

### 3. Run and limit each job

Use `fork()` and `execvp()`. Never use `system()`. Follow Section 5A's order:

1. Prepare and enter the job directory. When running as root, give the job files
   the required ownership so the unprivileged child can use them.
2. Resolve the dummy user with `getpwnam()`, then call `setgid()` before
   `setuid()`. Check failures and refuse to execute if privilege dropping fails.
3. Set RLIMIT_CPU using the job's time limit.
4. Set RLIMIT_AS using RAM converted from MB to bytes.
5. Set RLIMIT_NPROC, initially 64. This budget is shared by jobs using the same
   dummy user, so test concurrent jobs as well as a single job.

Without root, print the required warning and skip ownership changes and
privilege dropping. Also enforce a separate monotonic wall-clock deadline;
sleeping jobs must time out too. On macOS, monitor RSS with `proc_pidinfo()` and
kill the job when it exceeds its RAM limit because RLIMIT_AS is ignored there.
Cores and GPUs are scheduler accounting, not OS-enforced limits.

Start with one worker event loop using `poll()`, nonblocking child reaping with
`waitpid(..., WNOHANG)`, and periodic timer checks. It must support multiple
active jobs and keep processing scheduler messages while jobs run.

### 4. Capture output and finish cleanly

- Redirect both stdout and stderr into the job's `output.log` before exec.
  Job output must not appear directly in the worker terminal.
- Read and send newly appended bytes roughly every 500 ms, starting at offset 0
  for each run. This is already required in Milestone 1.
- On exit, reap the child, send remaining output, then send JOB_FINISHED with
  the exit result or known failure reason. Preparation/fork failures before
  JOB_STARTED use JOB_REJECTED instead.
- Report wall-clock timeout as a timeout rather than a bare SIGKILL result.
- Remove the job directory only after its final output/result have been queued
  safely for sending. The worker can continue running other jobs.
- Use the shared logger for assignment, execution, failure, cleanup, and
  connection/discovery messages.

### How to work without the scheduler

Build a local runner first that accepts a job specification and executes a test
program. Test limits and output without sockets. Then feed protocol assignments
from a fake scheduler and connect to Tushita's scheduler when registration works.

### Teja's completion checks

- A PATH command and a transferred executable both run.
- A successful job, a nonzero exit, and a missing command report distinct results.
- An infinite loop and a sleeping job both respect the wall-clock deadline.
- Memory enforcement works on the worker's OS, including macOS RSS checks.
- Both output streams reach the scheduler, including output produced before exit.
- Concurrent jobs keep separate files/output, and every child is reaped.
- Test privilege dropping under sudo before final integration.

Suggested commit sequence:

1. `Add worker capacity checks and scheduler registration`
2. `Receive executable transfers and prepare job directories`
3. `Run jobs with privilege dropping and resource limits`
4. `Capture incremental output and report job results`
5. `Handle concurrent jobs and clean up after disconnects`

## Me: client and shared modules

Main files belong under `src/client/`, `src/common/`, and `include/common/`.
The transport work already done is the first part of this responsibility.

### 1. Finish the shared payload codecs

- Start with CLIENT_LOGIN, WORKER_REGISTER, WELCOME, and END so registration can
  be integrated first.
- Then cover the remaining Milestone 1 requests, replies, events, dispatch,
  file-transfer, output, and result messages.
- Use explicit big-endian fields and length-prefixed strings/bytes. Do not send
  native structs. Reject malformed lengths, invalid fields, and trailing bytes.
- Put declarations and payload records in a shared header, for example
  `include/common/messages.h`, with implementation in `src/common/messages.c`.
- Give Tushita and Teja the function signatures early so their handlers can use
  the same records. They should not write their own competing serializers.

### 2. Add shared discovery and logging

- Implement UDP request/reply encoding, broadcast discovery, and responder
  helpers in shared modules. Use the datagram layout in PROTOCOL.md.
- Discovery uses three attempts with two seconds per attempt and logs the
  outcome. Advertise a reachable TCP address and respect custom discovery ports.
- Tushita owns starting the scheduler responder. Teja and I own calling discovery
  from worker/client startup and reconnect paths respectively.
- Implement one thread-safe `log_line()` helper with `[HH:MM:SS]` timestamps and
  uppercase component tags. Tushita and Teja call it from their modules.

### 3. Build the interactive client

- Discover/connect, prompt for a valid username unless `-user` was supplied,
  send CLIENT_LOGIN, and wait for successful login before accepting commands.
- Implement `nodes`, `submit`, `status <id>`, and `logs <id>`.
- For submit, require positive cores/RAM/time and priority 1..3. GPU defaults to
  zero. Send the executable path/name, not file contents, to the scheduler.
- Parse commands locally and show clear usage for bad input. Handle scheduler
  errors too, including QUEUE_FULL and unknown job IDs.
- Display worker resources, job owner/state/assignment, retained output, and
  submission IDs from structured responses.
- Use `poll()` over stdin and the socket so events appear while the prompt is
  idle. Distinguish responses by request ID and unsolicited events by ID 0.
- After a reconnect, send the saved username again without prompting. Do not
  automatically replay submissions whose responses might have been lost.

### 4. Keep shared tests and integrate the client

- Maintain transport tests and add codec/discovery tests as those modules land.
- Use a fake scheduler to test responses, errors, and events before Tushita's
  handlers are ready. Plain log retrieval should return to the prompt.
- Work with Tushita on login, submission, queries, and events, then with both
  teammates on the complete job flow and cross-machine discovery.

Suggested commit sequence:

1. `Implement shared TCP framing and buffered connection I/O` (already present)
2. `Add login and registration payload codecs`
3. `Add remaining core job and query payload codecs`
4. `Implement UDP discovery and shared timestamped logging`
5. `Add client login and command parsing`
6. `Handle client responses and asynchronous job events`
7. `Add core pipeline integration tests`

## How we connect the work

| First working flow | Who needs to integrate | What proves it works |
| --- | --- | --- |
| Worker registration | Teja + Tushita, using my codecs/transport | Scheduler assigns a worker ID and nodes shows its resources |
| Client login | Me + Tushita | Username is accepted, including two sessions with the same name |
| PATH job | All three | Submit `ls`, receive a job ID, run it, retrieve output and COMPLETED status |
| Transferred executable | Tushita + Teja, then my client | Executable exists only on scheduler before submission and runs on worker |
| Discovery | My shared helpers + Tushita's responder + both callers | Client and worker find scheduler without fallback on a personal hotspot |
| Limits and scheduling | All three | Queueing, failure events, output, and resource release agree |

For the first registration check, temporary explicit-address connections are
fine during development. Milestone 1 is complete only when final startup uses
discovery first, even when a fallback address is supplied.

Tushita and Teja should start now. My next task is the four registration/login
codecs. Tushita can build queue/resource functions in parallel; Teja can build
the local runner. Neither needs to wait for the finished client.

## Working rules and final checks

- Each person owns changes in their main directory and commits their own work.
  If someone needs a shared API change, agree on it before changing callers.
- Shared modules are my primary ownership. Tushita and Teja still validate their
  domain rules and integrate the APIs into their programs.
- Keep modules small. Include a working example or meaningful check with each
  substantial commit. Commit messages should describe the behavior added.
- Wire IDs, fields, and reply order come from PROTOCOL.md. Record agreed changes
  there in the same commit as the corresponding shared-code change.
- Merge a small working flow regularly. Review failures together; integration
  fixes are part of all three roles.
- Run `make` and `make test`, then run all three programs on one machine and
  across compatible machines. Transferred binaries must match the worker OS
  and CPU architecture; this project does not translate executables.

Before calling Milestone 1 done, check multiple clients/workers, owner-only
events at an idle prompt, FIFO blocking, PRIORITY aging, full-queue rejection,
resource release, binary transfer, stdout/stderr capture, execution limits,
timestamped logs, and discovery fallback. Test on Linux and macOS.

Milestone 2 adds heartbeat-based failure detection/retries, load-aware worker
selection, cancellation, live telemetry, detailed crash diagnosis, client
`logs -f`, and draining. Worker output updates every 500 ms and basic time/memory
enforcement are already Milestone 1 work. Dependencies, quotas, preemption,
cron, caching, checkpoints, and migration come after that.
