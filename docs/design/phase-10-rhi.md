# Phase 10 — RHI design: the object model, lifetime, and the boundary

Date: 2026-10-25
Status: approved
Related: ADR 0002 (math and depth), ADR 0006 (allocation and handles),
ADR 0008 (framebuffer and present), ADR 0013 (RHI object model),
[`cyber_rhi` contract](../../engines/cyber_rhi/docs/MODULE.md),
[API comparison notes](../notes/phase-10-api-comparison.md)

## What this phase builds

Phase 10 is the interface every GPU path is built on, so it defines far more
than it implements. It delivers:

- **An object model.** Eleven object types — device, queue, command list,
  buffer, texture, texture view, sampler, shader, pipeline, binding group,
  fence, swapchain — each referred to by a generation-counted handle. The
  header that declares them is the contract for Phases 11 to 15 and 34.
- **A backend boundary.** A plain function-pointer table that a backend fills
  in, with no virtual functions, no RTTI, and no allocation callbacks. The null
  backend implements it in this phase; D3D12 does in Phase 14 and Vulkan in
  Phase 34.
- **The device surface that exists today.** Adapter enumeration, capability
  queries, device creation with one queue per class, timeline fences, and
  `WaitIdle`, all implemented and tested on the null backend.
- **The conventions at the boundary**, written down as constants with a
  per-backend mapping table: +Y up in clip space, reverse-Z in a 0..1 range,
  top-left texture origin, right-handed view, column-major matrices.
- **Lifetime, threading, error, and validation rules**, as a contract rather
  than as folklore in a backend.

The roadmap done-when line is: *the interface headers compile and a null backend
(does nothing) links against them.* The build guide adds that the owner reviews
the design document; that review is this file and ADR 0013.

Note on numbering: the build guide names the ADR `0001-rhi-object-model.md`, but
0001 is already the module-architecture record. The object-model decision is
therefore ADR 0013, and this note is the cross-reference.

## What it deliberately leaves out

- **Resource creation** (buffers, textures, samplers, views) is Phase 11. This
  phase declares the handle types and the memory-class enum they will use, so
  the ABI shape is fixed now and Phase 11 fills in descriptors.
- **Pipelines and binding groups** are Phase 12: layouts, hashable descriptor
  structs, and the D3D12 root-signature mapping.
- **Command lists, barriers, and the swapchain** are Phase 13. The handle types
  and the queue-class enum exist; `BeginCommandList` and friends do not.
- **GPU memory allocation, naming, markers, and timestamps** are Phase 15. The
  design note records where they attach so they do not leak into the interface
  later.
- **The plugin ABI.** The stable C API is Phase 25; this header set is C++
  with templates and inline helpers and is explicitly not stable.

Keeping these out is the point of the phase: eleven handle types, one device
lifecycle, one fence, and zero guesses about resource descriptors that Phase 11
has not designed yet.

## Interfaces it touches

New public headers under `engines/cyber_rhi/include/cyber/rhi/`:

| Header | Contents |
|---|---|
| `handle.h` | `Handle<Tag>` aliases for every object type, validity, and 64-bit packing. |
| `format.h` | `Format` enum, `FormatName`, depth/stencil and sRGB classification. |
| `conventions.h` | Boundary constants and the per-backend mapping predicates. |
| `capabilities.h` | `BackendKind`, `AdapterInfo`, `Limits`, `Feature`, `Features`, `Capabilities`. |
| `backend.h` | `Backend`, `DeviceInterface`, `NullBackend()`, `EnumerateAdapters`. |
| `device.h` | `QueueKind`, `DeviceDesc`, `Device`, `CreateDevice`, fences, `WaitIdle`. |
| `rhi.h` | Umbrella include. |

Private implementation: `src/null_backend.cpp` (the null device, its object
table, and its fence timeline).

Changed: `engines/cyber_rhi/CMakeLists.txt`, `engines/modules.json` (status),
`engines/MODULES.md` (generated), `engines/cyber_rhi/README.md`,
`engines/cyber_rhi/docs/MODULE.md`.

Nothing in another module changes. `cyber_rhi` depends on `cyber_core` and
`cyber_memory` only for this phase; `cyber_platform` enters in Phase 13 when a
swapchain needs a surface, and `cyber_perf` in Phase 15 when markers and
timestamps exist.

## Design

### Handles

Every object except the device is a handle:

```cpp
template <typename Tag>
struct Handle
{
    u32 index = kInvalidHandleIndex;  // 0xFFFFFFFF for "no object"
    u32 generation = 0;               // bumped on destroy; never zero
};
```

This is the rule ADR 0006 set for `SlotMap`, reused here so the RHI and the
container agree on what a stale handle is. A handle is trivially copyable, 8
bytes, and comparable; `PackHandle`/`UnpackHandle` give a stable 64-bit form for
logging and for the future C ABI, with the index in the high 32 bits so a sorted
log reads like a slot order.

The device is a move-only value, not a handle, because it *owns* the object
table. A handle to a device would need a second table to find the first, and the
device is never shared between threads by reference in a way that needs one.
Destroying the device destroys every object it owns and invalidates every
handle it ever issued: `Device::~Device` calls `WaitIdle` first, then the
backend's destroy.

Handles are typed by tag, so `BufferHandle` and `TextureHandle` cannot be
converted to one another. That is the whole point: the memory bug the type
system catches is a buffer passed where a texture belongs, and a void pointer
would not catch it.

### Queues

Three classes: graphics, compute, copy. A device exposes at most one of each in
Phase 10 (`Queue(QueueKind)` returns an invalid handle for a class the backend
does not have). Multi-queue submission and cross-queue sync are Phase 13, but
the enum and the handle exist now so the command-list API does not have to
change its signature later.

The null backend reports all three classes and aliases them onto one internal
queue, and it records that aliasing in its capabilities so a test can assert the
honest answer. A backend with one physical queue must be able to say so rather
than silently serializing.

### Timeline fences

A fence is a monotonic 64-bit timeline:

```cpp
Result<FenceHandle, Error> CreateFence(const FenceDesc&);   // initial value
Status<Error> SignalFence(FenceHandle, u64 value);
Status<Error> WaitFence(FenceHandle, u64 value, u64 timeoutNanoseconds);
u64 GetFenceValue(FenceHandle) const;
```

This is the shape D3D12 has natively, Vulkan 1.2 has with timeline semaphores,
and WebGPU does not have at all. A backend without values emulates them; the
interface does not lose the value because a copy queue needs a *number* to wait
on, and a binary fence cannot express "wait for frame N".

`WaitFence` takes a timeout in nanoseconds, `0` meaning "poll" and
`kInfiniteTimeout` meaning "block". A timeout is a reported failure, not a
crash: the call returns `ErrorCode::NotFound` when the value was not reached,
which is deliberately distinct from the `InvalidArgument` a stale handle
returns. Recording both as one code would make a debugging session guess whether
the GPU was slow or the caller was wrong.

### Objects the RHI does not own

A swapchain is created from a surface the *caller* owns (an HWND today, a
Cyber Engine host window at Phase 25). The RHI never creates a window; that is
`cyber_platform`. The design keeps the surface opaque at this boundary:

```cpp
struct SurfaceDesc
{
    const void* nativeWindow = nullptr;  // non-owning; HWND on Windows
    u32 width = 0;
    u32 height = 0;
};
```

Phase 13 fills in the swapchain creation; this phase only records the decision
that the handle is opaque and the surface is borrowed.

### Capabilities

`Capabilities` is a value, not a handle: an `AdapterInfo`, a `Limits` struct of
documented maxima, and a `Features` bitset. Querying it costs no allocation and
is safe from any thread, because a backend answers it from a cached struct.

The limits are deliberately conservative and explicit rather than "whatever the
API says". A caller that needs 8 color attachments, 16 vertex buffers, or
1 GiB buffers reads `Capabilities` and plans; a backend that cannot do it fails
the call rather than the frame.

Optional features are a bitset with a fixed list in this phase. The list grows
only when a phase's design note names a feature and a backend that can report
it: `SwapchainTearing`, `TimelineFences`, `DebugMarkers`, `TimestampQueries`,
`Compute`, `IndirectDraw`, `MultiDrawIndirect`, `AnisotropicFiltering`,
`BindlessResources`, `MeshShaders`, `GeometryShaders`, `StorageTextures`.
An unknown feature cannot be requested because the enum is closed; that is
intended, and adding one is an additive change to a header that is not yet
stable.

### Conventions at the boundary

The boundary is the engine's convention, not any API's:

| Item | Value | Why |
|---|---|---|
| Clip-space Y | +Y up | Matches the engine's right-handed, Y-up world and the software renderer, so a projection matrix is identical on both paths. |
| NDC depth | 0..1, reverse-Z: near = 1, far = 0 | ADR 0002. Native on D3D12, Vulkan, and WebGPU. |
| Texture origin | top-left | Native on all three of those; Metal flips V in the sampler. |
| View | right-handed, column vectors | ADR 0002, ADR 0005. |
| Matrices | column-major in memory | Shipped as-is; D3D12's constant path transposes once in Phase 12. |
| Units | meters, radians, linear RGB | ADR 0002. |

`conventions.h` exposes these as constants plus three predicates a backend uses
instead of hard-coding its own answers:

```cpp
constexpr bool BackendNeedsClipSpaceYFlip(BackendKind) noexcept;
constexpr bool BackendUsesZeroToOneDepth(BackendKind) noexcept;
constexpr bool BackendUsesTopLeftTextureOrigin(BackendKind) noexcept;
```

A table test asserts all three for every `BackendKind`, so a backend added later
cannot quietly disagree with the boundary.

### The backend boundary

A backend is a struct of function pointers plus two factories:

```cpp
struct Backend
{
    BackendKind kind;
    const char* name;
    u32 apiVersion;                       // 0 for the null backend
    u32 (*enumerateAdapters)(AdapterInfo* out, u32 capacity) noexcept;
    Result<DeviceCreation, Error> (*createDevice)(const DeviceDesc&) noexcept;
    void (*destroyDevice)(void* internal) noexcept;
    const DeviceInterface* interface;
};

struct DeviceInterface
{
    Capabilities (*capabilities)(const void* internal) noexcept;
    QueueHandle (*queue)(const void* internal, QueueKind) noexcept;
    Status<Error> (*createFence)(void* internal, const FenceDesc&, FenceHandle* out) noexcept;
    Status<Error> (*destroyFence)(void* internal, FenceHandle) noexcept;
    Status<Error> (*signalFence)(void* internal, FenceHandle, u64) noexcept;
    Status<Error> (*waitFence)(void* internal, FenceHandle, u64, u64) noexcept;
    u64 (*fenceValue)(const void* internal, FenceHandle) noexcept;
    Status<Error> (*waitIdle)(void* internal) noexcept;
};
```

No virtual functions: a vtable would work, but the C ABI in Phase 25 needs a
table, the D3D12 backend in Phase 14 will be one translation unit, and a table
makes it obvious in a code review when a method is missing. There is no `void*`
user-data parameter because the backend's `internal` pointer already is the
device; a backend that needs more stores it behind that pointer.

`DeviceCreation` is `{ const DeviceInterface* interface; void* internal; }`.

### Lifetime

1. **The device owns everything.** Objects are destroyed only by the device, and
   destroying the device destroys what is left. There is no per-object
   reference count.
2. **Handles are invalidated on destroy.** `DestroyFence(h)` bumps the generation
   of slot `h.index`, so the handle the caller still holds no longer resolves.
   The backend detects that and returns `InvalidArgument` instead of touching a
   freed slot. A double destroy is therefore a reported error, not a crash and
   not silent success.
3. **Deferred destruction is a device rule, not a caller rule.** From Phase 13 a
   destroyed object whose work may still be in flight goes into a retire list
   keyed by fence value; `WaitIdle` and the frame loop both drain it. Phase 10
   has no submission, so the null backend retires immediately and the rule is
   recorded here so Phase 13 adds a mechanism rather than a rule.
4. **The device outlives its queue handles.** Queue handles are owned by the
   device and never destroyed individually.
5. **No object outlives the device.** `Device` is move-only and destroys in its
   destructor; a caller that copies a handle into a longer-lived structure is
   holding a value that will fail to resolve, which is the desired failure mode.

### Threading

| Object | Rule |
|---|---|
| `Device` (capabilities, queue lookup, fence creation) | Thread-safe: the null backend locks its object table. A backend whose native API needs external synchronization does the locking inside. |
| `Fence` operations | Thread-safe. `SignalFence` and `WaitFence` are atomic with respect to each other on the same fence. |
| `CommandList` (Phase 13) | Not thread-safe, and not thread-affine: one thread at a time, any thread, enforced by an atomic owner flag in debug builds. |
| `Swapchain` (Phase 13) | Not thread-safe; acquire and present from one thread. |
| Resource creation (Phase 11) | Thread-safe, same reason as the device. |
| Handles | Plain values; copying one between threads is safe, using it concurrently is governed by the object's rule. |

The lock is a single mutex in the null backend. That is honest for a validation
backend and measured in Phase 14: a spinlock or a per-slot seqlock is not worth
designing before a real submission path exists.

### Errors and validation

Every fallible call returns `Result<T, core::Error>` or `Status<core::Error>`,
with the codes from `core::ErrorCode` plus one RHI addition:

- `InvalidArgument` — a malformed descriptor, an invalid handle, or a destroyed
  object.
- `Unsupported` — a valid request the backend cannot honour (a feature the
  device does not report, a limit exceeded).
- `NotFound` — a wait that timed out.
- `Internal` — the backend failed for a reason the caller cannot fix.
- `DeviceLost` — added by Phase 14 with its own code, because a lost device is
  recoverable only by recreating it and a caller must distinguish it from a bug
  in its arguments. Phase 10 records the decision and does not add the enum
  value yet: the null backend never loses a device, and an enum value with no
  producer is untested code.

Debug-only validation is the null backend's job, and it validates every public
operation: handle validity, descriptor ranges, null output pointers, and
capability limits. In release builds the checks stay in the null backend (it has
no other work to do) and are compiled out of real backends where a driver call
would fail anyway.

### What the null backend does

It is a real backend that allocates nothing on the GPU and does the bookkeeping
of one:

- Issues one synthetic adapter: `"CyberGraphics Null Device"`, vendor 0, device
  0, 0 bytes of video memory, `apiVersion` 0.
- Reports conservative limits (4096 texture 2D, 1 GiB buffer, 8 color
  attachments, 4 binding groups) and a feature set that says `Compute`,
  `TimelineFences`, and `IndirectDraw` are supported, because a validation
  backend must be able to *accept* the calls it validates.
- Implements the timeline fence with a plain `u64` per fence and a condition
  variable, so `WaitFence` really waits and a timeout really times out.
- Tracks its object table so a stale handle is detected (Phase 11 extends the
  same table to resources).

## Alternatives considered

- **One `IRhiDevice` abstract class per backend.** Virtual calls are the obvious
  shape, but the project disables RTTI and the Phase 25 ABI is a C table; a
  vtable would have to be re-expressed as a table anyway, and a table is
  inspectable in a debugger without a symbol for the class.
- **A single `Rhi` singleton with free functions.** Rejected: the project forbids
  global mutable state, and a device must be passable explicitly so a tool can
  hold two.
- **Raw `uint64_t` handles.** Rejected: no type safety, and the generation field
  disappears into bit twiddling at every call site.
- **Reference-counted resources.** Rejected by ADR 0006 and by the frame model: a
  resource's lifetime is bounded by a fence value, not by who points at it.
- **Requiring the caller to manage command pools and allocators (Vulkan style).**
  Rejected for Phase 10: the RHI hands out a command list per frame and recycles
  the pool internally, which is what all three APIs can express.
- **Implicit barriers (WebGPU style).** Rejected: D3D12 and Vulkan require
  explicit transitions, and a hidden barrier is neither predictable nor
  measurable. Explicit transitions with a no-op implementation on WebGPU is the
  smaller lie.
- **Making the device a handle too.** Rejected: the device owns the table that
  resolves handles, so a device handle needs a table to find the table.

## Risks

- **The interface can ossify before a real backend tests it.** Mitigation: keep
  it small, and let Phase 14 change it, since nothing is stable until Phase 25.
  The integration test in Phase 14 is the first real proof.
- **A conservative `Limits` struct may not match D3D12 tier 1.** Mitigation: the
  struct is data; Phase 14 fills it from `CheckFeatureSupport` and a test asserts
  the values are internally consistent (max texture ≥ max cube face, and so on).
- **Timeline fences on Vulkan without the feature.** Mitigation: the emulation is
  a backend-local map; the interface never exposes a semaphore.
- **The single mutex in the null backend serializes creation.** Acceptable: it
  exists to validate, and Phase 15 measures submission cost rather than creation.
- **Handle packing into 64 bits assumes 32-bit indices forever.** Acceptable:
  `u32` index and generation are already the `SlotMap` rule; a 64-bit index would
  change `SlotMap` first.
- **The convention table can drift from a backend.** Mitigation: the table test
  plus one golden image per backend in Phase 14, compared against the software
  renderer with the same scene.

## Test plan

Null-backend tests in `engines/cyber_rhi/tests/`:

- `rhi_handle` — validity, equality, stale-handle detection after destroy, and
  pack/unpack round trip including the invalid handle.
- `rhi_format` — every enumerator has a name, depth formats classify as depth
  and not sRGB, sRGB formats classify as sRGB and not depth, and the count
  sentinel is consistent.
- `rhi_conventions` — the per-backend table agrees with the boundary constants
  for all five kinds.
- `rhi_capabilities` — the null adapter exists, its limits are non-zero and
  internally consistent, its features parse, and `Capabilities` is a value type.
- `rhi_device` — create, queue lookup per class, capabilities, `WaitIdle`,
  destroy; a moved-from device has no capabilities and rejects use.
- `rhi_fence` — signal then wait returns immediately, wait with a zero timeout
  on an unreached value returns `NotFound`, a wait really blocks until another
  thread signals, invalid and destroyed handles return `InvalidArgument`, and
  the timeline never goes backwards.

All tests run in all four presets, plus an ASan run under the existing CI job.

## Sources

- The API comparison notes in `docs/notes/phase-10-api-comparison.md` list the
  primary documents; the decisions above cite them by section.
- ADR 0002 (math and depth conventions), ADR 0006 (allocation and handles),
  ADR 0008 (framebuffer and present), ADR 0013 (this phase's object model).
