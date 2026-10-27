# 0018 — GPU heap sub-allocation and one RHI-level validation layer

- **Status:** proposed
- **Date:** 2026-10-27
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 15, [design note](../design/phase-15-gpu-memory-debug.md),
  ADR 0006 (memory allocation and handles), ADR 0013 (RHI object model), ADR 0014
  (resource model), ADR 0016 (command recording), ADR 0017 (D3D12 backend),
  [`cyber_rhi` contract](../../engines/cyber_rhi/docs/MODULE.md), [Phase 14
  report](../reports/phase-14.md)

## Context

Phase 14 gave every resource its own `ID3D12Heap` and left every check to the
driver's debug layer. Both were the right trade for landing a first backend and
both are wrong as a resting state. A scene with thousands of resources pays a
heap object per resource, loses every opportunity to reuse memory between
resources whose lifetimes do not overlap, and cannot report what it costs. And a
debug-only, driver-side, process-killing check is not the engine's own
validation: it does not exist in a shipping build, it cannot return a status to
a caller, and its message describes a D3D12 call rather than the RHI promise
that call broke.

The pieces that decide the shape are already fixed. `ValidationLevel` exists in
`DeviceDesc` with Phase 15's meaning written next to it (`Standard` is validity
and limits, `Exhaustive` adds state tracking, leak reports, and per-call
logging). `Feature::DebugMarkers` and `Feature::TimestampQueries` exist in
`capabilities.h`, unused. `BufferDesc::label` and `TextureDesc::label` exist, so a
leak report can name what it finds. The null backend already carries a full rule
set — argument validation, a recorded state machine, per-subresource state,
binding completeness — which is the rule set a real backend is missing.

Doing nothing leaves three phase-15 promises unmet, and the ones the done-when
line names first: a deliberately wrong barrier must be caught with a clear
message, pass timings must be visible, and a leak report must name a leaked
resource.

## Decision

1. **Sub-allocation inside large heaps, per memory class.** The backend keeps a
   block per class (device-local, upload, readback) carved from one `ID3D12Heap`,
   growing by doubling from 8 MiB to 256 MiB. Inside a block a free list sorted
   by offset serves first-fit requests rounded up to the API's placement
   alignment (64 KiB, 4 MiB for multisampled textures). A request no block can
   serve gets a dedicated heap, which is today's behaviour, so the allocator is
   a fast path and not a new failure mode. Heaps are never moved or
   defragmented: Phase 20 owns relocation, because moving memory needs a frame
   boundary.
2. **Aliasing is opt-in and checked.** `BufferDesc` and `TextureDesc` gain
   `MemoryLifetime { Retained, Transient }`, defaulting to `Retained`, which is
   exactly today's behaviour. Only a transient resource's memory may be reused,
   and reuse is legal only once the fence guarding the previous owner's last use
   is reached. The validation layer performs that comparison and reports an
   early reuse as an error naming both resources.
3. **One validation implementation, two callers.** The checks move out of
   `null_backend.cpp` into `cyber_rhi/src/validation.cpp`. The null backend
   calls them while performing the work; a wrapping `DeviceInterface` and
   `CommandListInterface` call them while forwarding to a real backend. A rule
   is therefore written once, and a backend cannot enforce a different version
   of it.
4. **The RHI owns the checks the driver cannot make.** Handle validity, state
   and barrier legality, binding completeness, capability limits, destroy while
   in flight, descriptor exhaustion, and aliasing legality. The driver's debug
   layer stays on in debug builds as a second, independent opinion, and its
   messages remain test failures (ADR 0017).
5. **The layer is transparent and off by default in release.** With
   `ValidationLevel::Off` a caller gets the backend's own argument checks and
   nothing else; `Standard` adds the validation implementation; `Exhaustive`
   adds a per-call trace and the leak report at device destruction. The
   D3D12 suite has to pass with the layer in front of the backend, which is how
   the layer's transparency is proved rather than asserted.
6. **Additive public interface only.** `ErrorCode::DeviceLost`, the memory
   lifetime fields, `MemoryStats`/`Device::MemoryStats()`, the query-heap and
   timestamp calls, and `Device::ReportLeaks()`. No existing call changes
   meaning, and no existing description becomes invalid.
7. **A removed device stays removed.** Submission, present, and fence waits
   detect removal, report the reason and the DRED breadcrumbs on the `Rhi`
   channel, and every later call returns `DeviceLost`. The backend never
   recreates itself: a device that silently rebuilt its objects would leave
   every handle its caller holds stale but valid-looking.

## Consequences

### Positive

- Memory cost becomes a number a caller can read, per class, before it becomes a
  failed frame; the largest servable request is reported alongside the waste.
- A resource per heap disappears for ordinary resources, and a transient
  resource can share memory with a resource that has finished with it.
- Every barrier, binding, and lifetime mistake the RHI can name is caught in a
  release build, with the resource named, on the backend that is running.
- The rule set is one file that two backends and one wrapper share, so adding
  the Vulkan backend in Phase 34 does not mean writing the rules a third time.
- Pass timings and a leak report turn "it is slow" and "memory grows" into
  numbers and names.

### Negative

- First fit without compaction will refuse a request a compacting allocator
  would serve. This is accepted and instrumented, not solved here.
- Aliasing is only as correct as the caller's promise plus the fence check; a
  hole in that check surfaces as a wrong pixel under load, which is expensive to
  attribute. The deliberate-early-reuse test exists to keep the check honest.
- The validation layer is code on the record path. It is off in release by
  default and allocation-free, but `Exhaustive` is genuinely slower.
- The allocator adds a failure mode of its own — an out-of-memory block — where
  a resource-per-heap device failed one resource at a time.
- `MemoryLifetime::Transient` is a promise the RHI cannot verify at creation
  time, only at reuse time, so misuse is caught late rather than at the call
  that made it.

### Neutral

- `ValidationLevel` gains meaning at the RHI level without changing its
  enumerators. Code that already passes `Standard` starts getting more checks,
  which is what the enumerator's comment always said it would do.
- Timestamps are advertised only where the adapter reports support. On WARP the
  feature may be absent, and the test reports the skip rather than pretending.

## Alternatives considered

| Alternative | Why not |
|---|---|
| Keep one heap per resource | It cannot reuse memory between non-overlapping lifetimes, and it keeps the per-resource cost the phase exists to remove. |
| A GPU allocator with defragmentation | Relocation needs a frame boundary and a mechanism to move live resources; that is the render graph's, in Phase 20. |
| Put the validation layer in the D3D12 backend | The rules are the RHI's, not D3D12's, and a second backend would implement them differently; the null backend already proves one implementation can serve both. |
| Rely on the debug layer's validation and GPU-based validation | Debug-only, process-fatal, D3D12-shaped, and absent in the build that ships. |
| Validate in the `Device` and `CommandList` front ends above the backend | Handles and state live below those classes, and the front ends are shared by every backend; the wrapper keeps the checks where the backend's own answers already are. |
| Aliasing driven by the frame graph only, with no RHI rule | The graph decides when reuse is desirable; only the submission timeline can say whether it is legal. |
| Read timestamps back on the frame that wrote them | It reads a query heap the GPU may still be filling, which is the hazard the frame ring exists to prevent. |
| Treat a removed device as `Internal` | The caller can neither recognise it nor act on it, and the reason code is lost. |

## Follow-up

- The phase lands in the seven increments the design note lists, each with its
  own test and its own commit; the roadmap checkbox waits for the leak-report
  and timings evidence the done-when line asks for.
- `docs/debugging.md` lands with increment 4, when names and markers exist for it
  to describe.
- Phase 20 consumes `MemoryLifetime::Transient` and the allocator's statistics;
  if the graph needs an allocation API the RHI does not have, that is a
  follow-up ADR rather than a field added quietly.
- Phase 25's capture format reads the markers and timestamps this phase adds; a
  capture that cannot name a pass is a follow-up problem there, not here.
