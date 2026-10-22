# cyber_jobs — module contract

**Kind:** service
**Layer:** 1
**Order:** 12
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

Job system: fixed worker pool, per-worker work-stealing queues, dependency
counters, generation-checked handles, parallel-for, deterministic shutdown, and
profiler scope markers.

## Public API

`include/cyber/jobs/`

| Header | Contents |
|---|---|
| `job_system.h` | `JobSystem`, `JobDesc`, `JobHandle`, `JobTag`, `JobFunction`, `ParallelFunction`. |
| `jobs.h` | Umbrella include. |

Only headers under this path are public. Everything under `src/` is private.
Public headers are self-contained.

### Job contract

- `JobFunction` and `ParallelFunction` are non-throwing function pointers.
  User data is a non-owning `void*`; the caller keeps it alive until the job
  completes.
- `JobHandle` is a generation-counted index. `Wait(handle)` waits for completion
  and consumes the handle. A forgotten handle keeps its job record alive until
  `Shutdown`.
- `SubmitDetached` is fire-and-forget; it returns `false` when the system is
  stopping or no worker queue has room.
- `SubmitAfter(dependency, desc)` runs after the dependency completes. If the
  dependency is already complete, the job is submitted immediately. An invalid
  dependency returns an invalid handle.
- `Wait` helps execute one ready job before blocking. `WaitIdle` waits for all
  active work without helping, so throughput measurements reflect worker count.
- `ParallelFor(count, batchSize, function, userData)` calls `function` once for
  every index in `[0, count)`, using detached batch jobs and an atomic cursor.
  It waits until every batch completes. Nested `ParallelFor` from a worker is not
  supported in the first pass.
- `Shutdown` stops admission, drains queued work, and joins every worker. It is
  idempotent. The destructor calls it.
- A job must never wait on unrelated work that cannot make progress.

## Dependencies

Allowed: cyber_core, cyber_memory, cyber_platform, cyber_perf

Forbidden: cyber_rhi, cyber_renderer, cyber_graphics

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- A job never waits on work that cannot make progress.
- Every submitted job has explicit dependency and completion rules.
- Shutdown is deterministic and joins every worker.
- No detached OS thread and no hidden `std::thread` destruction.
- Job records and worker queues use explicit allocators.
- Worker count is fixed for the lifetime of a `JobSystem`.
- Job handles validate index and generation; a stale handle does not resolve.
- Oversubscription is tested explicitly.

## Non-goals

- Fibers and job cancellation in the first pass.
- GPU scheduling and RHI command submission.
- Job priorities and per-worker affinities.
- Work stealing across process boundaries.
- Nested `ParallelFor` from a worker in the first pass.

## Tests

`jobs`: 10,000 detached jobs on 1, 2, 4, and hardware workers; dependency chains;
`ParallelFor` coverage; oversubscription; repeated create/shutdown cycles;
handle consumption; profiler scope balance; TSan on Linux CI.

`jobs_public_headers`: every public header compiles together.

## Budgets

10,000 jobs scale across all cores. Record wall time at 1, 2, 4, and hardware
workers on the target machine. Submission and completion are allocation-free
after creation; queue and job storage are preallocated.

## Roadmap

Phase 5.
