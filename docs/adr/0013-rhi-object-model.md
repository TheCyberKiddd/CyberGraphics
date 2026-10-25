# 0013 — RHI object model, handles, and the backend boundary

- **Status:** accepted
- **Date:** 2026-10-25
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 10, [design note](../design/phase-10-rhi.md),
  [API comparison notes](../notes/phase-10-api-comparison.md), ADR 0002,
  ADR 0006, ADR 0008

(The build guide names this record `0001-rhi-object-model.md`; 0001 is already
the module-architecture record, so the next free number is used.)

## Context

Every GPU path from Phase 11 to the plugin ABI in Phase 25 sits on the interface
this record describes. The three candidate APIs disagree in the places that
matter most: D3D12 creates command allocators per thread, Vulkan requires the
queue set at device creation, WebGPU has one implicit queue and no explicit
barriers at all. A renderer that adopts one API's shape pays for the other two
forever, and a renderer that abstracts too early hides costs it cannot measure.

The project also fixes constraints that remove several easy answers: no
exceptions, no RTTI, no global mutable state, no third-party runtime library, and
a stable C ABI at Phase 25 that a plugin implements.

## Decision

1. **Objects are generation-counted handles; the device is a value that owns
   them.** `Handle<Tag>` is `{ u32 index; u32 generation; }`, the same rule
   ADR 0006 set for `SlotMap`. Destroying an object bumps its generation, so a
   stale handle fails to resolve instead of aliasing a new object. The device is
   a move-only value because it owns the table that resolves handles.
2. **Three queue classes, at most one of each per device in this phase:**
   graphics, compute, copy. A backend with one physical queue aliases them and
   reports that in its capabilities.
3. **Fences are monotonic 64-bit timelines.** `SignalFence(value)` and
   `WaitFence(value, timeout)`. D3D12 has this natively, Vulkan 1.2 has it with
   timeline semaphores, WebGPU emulates it. A binary fence cannot express "wait
   for frame N" across queues.
4. **The backend boundary is a table of function pointers, not virtual
   functions.** One `Backend` struct with adapter enumeration and device
   creation, and one `DeviceInterface` table per device. No RTTI, no vtable to
   re-express for the C ABI, and a missing method is visible in a diff.
5. **Explicit synchronization is part of the interface from the start.** Barriers
   and timeline fences are caller-visible. A backend that cannot express a
   barrier (WebGPU) implements it as a no-op rather than the RHI inferring one.
6. **The boundary is the engine's convention, not the API's:** +Y up in clip
   space, reverse-Z in a 0..1 depth range, top-left texture origin, right-handed
   view, column-major matrices. Each backend maps its own API at the edge, using
   three predicates in `conventions.h` rather than its own hard-coded answers.
7. **Capabilities are a value with a closed feature enum and explicit limits.**
   A caller reads `Capabilities` and plans; a backend that cannot honour a
   request fails the call with `Unsupported` rather than failing a frame.
8. **The surface is borrowed, the swapchain is owned.** The RHI never creates a
   window; `cyber_platform` does, and the surface crosses the boundary as an
   opaque native handle.

## Consequences

- A stale handle is a reported `InvalidArgument`, not silent corruption. This
  costs one generation load per call and a table lookup in the null backend.
- Every object creation is a device call, so a tool cannot create a resource
  without a device. That is the intended ownership model, and it is what makes
  `Device::~Device` able to guarantee no leaks.
- The interface cannot describe anything the three APIs cannot all express
  without an emulation the design note names. Mesh shaders, sparse resources, and
  bindless resource arrays are therefore optional features, not core objects.
- Timeline fences cost a backend without them a value-to-semaphore map. That map
  is written in Phase 34 and is the reason fence waits take a value rather than a
  boolean.
- Function-pointer tables make a backend a link-time choice plus a runtime
  pointer, which is exactly the shape Phase 25's plugin loader needs. The cost is
  that a backend cannot add a private method; it must keep state behind its
  `internal` pointer.
- The device's single mutex serializes creation in the null backend. Phase 14
  replaces it with whatever the D3D12 path needs and measures submission, per the
  module's budget.
- `DeviceLost` is designed for but not defined in this phase: no null-backend
  path can produce it, and an error code with no producer and no test is worse
  than adding it with its first real producer.

## Alternatives

- **An `IRhiDevice` abstract base class per backend.** Rejected: RTTI is off, and
  the Phase 25 C ABI is a table, so the vtable would be re-expressed anyway.
- **One global RHI singleton.** Rejected by the no-global-mutable-state rule and
  by wanting two devices in a tool.
- **Raw `uint64_t` handles.** Rejected: no type safety between a buffer and a
  texture, and the generation field becomes bit manipulation at every call site.
- **Reference-counted resources.** Rejected: a resource's lifetime is bounded by
  a fence value, not by pointers; ADR 0006 already rejected reference counting
  for the same reason.
- **Implicit barriers with a render graph generating them (Phase 18's job).**
  Rejected at the RHI layer: the graph generates barriers *from* an explicit
  interface, and hiding them here would make a missed barrier unrepresentable and
  therefore untestable.
- **Exposing Vulkan-style command pools and allocators.** Rejected: the RHI
  recycles a list per frame, which all three APIs can express without the caller
  learning a pool's reset rules.

## Follow-up

- Phase 11 extends the same object table to buffers, textures, samplers, and
  views, and adds the format table.
- Phase 13 adds command lists, barriers, and the swapchain, including the retire
  list keyed by fence value that this record describes.
- Phase 14 is the first real backend and the first honest test of this interface;
  it may change it, and nothing here is stable until Phase 25.
