# 0014 — Resource descriptions, format metadata, and one state vocabulary

- **Status:** accepted
- **Date:** 2026-10-25
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 11, [design note](../design/phase-11-resources.md),
  [format table](../formats.md), ADR 0006, ADR 0013

## Context

Phase 10 fixed the object model but created no resources, on purpose: resource
descriptors are where an RHI either commits to a backend's shape or stays
neutral, and guessing them a phase early would have been the wrong kind of
progress.

Three facts shape the decision:

- D3D12, Vulkan, and WebGPU agree on what a buffer is and disagree about almost
  everything else. D3D12 wants heap properties and `ResourceStates`; Vulkan wants
  usage bits, image layouts, and per-aspect barriers; WebGPU has no barriers at
  all and validates usage at bind time.
- The renderer needs a *stable* set of concepts before Phase 14 writes the D3D12
  backend, because a backend that invents its own semantics silently becomes
  the definition.
- The project forbids exceptions, RTTI, global mutable state, and third-party
  runtime libraries, and Phase 25 exposes this interface as a C table.

## Decision

1. **Resources are created from plain aggregate descriptors.** `BufferDesc`,
   `TextureDesc`, `TextureViewDesc`, and `SamplerDesc` are trivially copyable
   structs passed by const reference. No builders, no setters, no hidden
   defaults that create a resource when zero-initialized. A zeroed descriptor is
   invalid and says so with `InvalidArgument`.

2. **Memory class is expressed as usage, not as an enum a backend translates.**
   `BufferUsage::HostVisible` means the CPU may write; `Readback` means the CPU
   may read. They are mutually exclusive and exclusive of every shader usage,
   which is exactly the D3D12 upload/readback rule and a legal Vulkan memory
   property selection. A caller that writes the invalid combination gets an
   error at creation rather than a validation layer message at draw time.

3. **One `FormatInfo` table is the single source of truth.** Bytes per block,
   block extent, channel count, component type, and the depth/stencil/sRGB/
   compressed flags all live in one `constexpr` switch. Every predicate is a
   projection of it, including the ones that already existed, so a new format
   cannot be half-classified.

4. **The RHI's state vocabulary is its own, and it is a bitmask.** Read states
   combine; at most one write state may appear; `CopyDestination` cannot combine
   with a read. `Undefined` on the `from` side discards contents and is the only
   `from` accepted without a match; `Undefined` on the `to` side is always
   invalid. State is tracked per `(mip, layer)`, not per aspect, because D3D12
   cannot express a per-aspect barrier and forcing Vulkan's model on every
   caller costs two barriers per attachment.

5. **A transition must name the state the resource is actually in.** A mismatch
   is `InvalidArgument` naming both states. This is the check that turns a
   missing barrier into a test failure in Phase 13 instead of a corrupted frame
   in Phase 14.

6. **Views borrow their texture; destroying the texture retires the views.**
   The backend tracks live views per texture and invalidates them together. A
   stale view handle never resolves into a newer one, which is ADR 0006's rule
   applied to a second object type.

7. **`Transition` is a device method in this phase and a command-list method in
   the next.** The signature does not change; only where the barrier is
   submitted moves. That keeps the state model testable before submission
   exists.

## Alternatives considered

- **A memory-class enum (`DeviceLocal`, `HostVisible`, `Readback`).** Rejected:
  it hides the capability the caller actually needs behind a translation the
  backend must implement three ways, and it makes "which of these can I write
  from the CPU" a lookup instead of a bit test.
- **A builder API.** Rejected: with no exceptions, a builder either holds a
  partial descriptor that can be misused or returns a `Result` from every
  setter. An aggregate is smaller, is the Phase 25 ABI shape, and serializes.
- **Vulkan-style single-valued states.** Rejected: a render graph wants to say
  "copy source and shader read at the same time" in one transition, and D3D12
  expresses that directly. A single-valued enum would force two barriers for a
  case both APIs accept.
- **Per-aspect state.** Rejected for the reason in decision 4; Phase 34 derives
  the second Vulkan layout from the one authoritative state.
- **A separate `Memory` object with explicit allocation.** Deferred to Phase 15.
  It is the right long-term shape for aliasing and sub-allocation, but it adds a
  lifetime layer before anything is submitted, and Phase 11 cannot test its
  value.
- **Typeless formats now.** Deferred to Phase 14. D3D12's typeless rules depend
  on the adapter's feature support; a rule invented now would be a guess.

## Consequences

- The null backend is a real reference implementation: it validates every rule
  above, allocates host memory for host-visible and readback buffers, tracks
  per-subresource state, and retires views with their texture.
- A backend must implement sixteen more table entries. A missing one is caught
  at device creation, not at the first call, because the device now validates
  the whole table.
- `cyber_rhi` gains four public headers and one table in `format.h`. A format
  added later must fill its row; the test walks every enumerator, so a forgotten
  row fails the build's test step rather than a frame.
- Error messages are static strings. Formatted diagnostics wait for Phase 15's
  error sink, which is when an allocation-free formatter can exist.
- Destruction is immediate in this phase and becomes retirement-queue based in
  Phase 13 without a public API change.
