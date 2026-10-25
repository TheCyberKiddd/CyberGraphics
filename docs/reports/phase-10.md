# Phase 10 report: RHI design

Date: 2026-10-25
Machine: Windows 11, 13th Gen Intel Core i7, MSVC 19.44, CMake 3.31
Design note: [phase-10-rhi.md](../design/phase-10-rhi.md)
ADR: [0013](../adr/0013-rhi-object-model.md)
Comparison notes: [phase-10-api-comparison.md](../notes/phase-10-api-comparison.md)
Contract: [`cyber_rhi`](../../engines/cyber_rhi/docs/MODULE.md)

## Done-when

> The interface headers compile and a null backend (does nothing) links against
> them.

Plus the build guide's extra requirement: the owner reviews the design document.
The review artifacts are the design note and ADR 0013.

Both are proved: `cyber_rhi` is a built library target, all eight public headers
compile standalone, the null backend links into it, and seven test binaries
exercise the interface against that backend.

## What was built

`engines/cyber_rhi/` goes from an empty scaffold to a module with a public
interface and a backend.

| Header | Contents |
|---|---|
| `handle.h` | Tags and aliases for eleven object types over `memory::Handle<Tag>`, plus `PackHandle`/`UnpackHandle`. |
| `format.h` | `Format` with 28 enumerators, `FormatName`, and depth, stencil, sRGB, block-compressed, and integer classification. |
| `conventions.h` | Boundary constants and `BackendNeedsClipSpaceYFlip`, `BackendUsesZeroToOneDepth`, `BackendUsesTopLeftTextureOrigin`. |
| `capabilities.h` | `BackendKind`, `AdapterInfo`, `Limits`, `Feature` (12 flags), `Features`, `Capabilities`. |
| `descriptors.h` | `QueueKind`, `ValidationLevel`, `DeviceDesc`, `FenceDesc`, `SurfaceDesc`, `kInfiniteWaitNanoseconds`. |
| `backend.h` | `Backend`, `DeviceInterface`, `DeviceCreation`, `EnumerateAdapters`, `NullBackend`. |
| `device.h` | `Device`: create, queues, capabilities, timeline fences, `WaitIdle`, move-only lifetime. |
| `rhi.h` | Umbrella. |

Private implementation: `src/device.cpp` (the façade and lifetime rules) and
`src/null_backend.cpp` (adapter enumeration, capability table, device, object
table, condition-variable fence timeline).

### What the interface actually implements today

Only what Phase 10 owns: adapters, capabilities, device creation and destruction,
one queue per class, and timeline fences. Buffers, textures, samplers, views,
pipelines, binding groups, command lists, barriers, and the swapchain have
handle types and documented semantics but no creation functions — those are
Phases 11 to 13, and inventing their descriptors now would pre-empt the design
that reads them.

### Example

```cpp
auto* backend = cyber::rhi::NullBackend();

cyber::rhi::AdapterInfo adapters[4];
const cyber::core::u32 count =
    cyber::rhi::EnumerateAdapters(*backend, cyber::memory::Span(adapters));

cyber::rhi::DeviceDesc desc{};
desc.backend = backend;
desc.adapterIndex = 0;
desc.validation = cyber::rhi::ValidationLevel::Standard;

auto created = cyber::rhi::Device::Create(desc, heap);
if (!created)
{
    CG_LOG(...created.Error().code...);
    return false;
}
cyber::rhi::Device device = std::move(created.Value());

auto fence = device.CreateFence(cyber::rhi::FenceDesc{});
device.SignalFence(fence.Value(), 3);
device.WaitFence(fence.Value(), 3);   // returns immediately; value already reached
```

## Decisions

- **Handles, not pointers.** `Handle<Tag>` is `{ u32 index, u32 generation }`
  (ADR 0006's `SlotMap` rule). `BufferHandle` and `TextureHandle` are the same
  bytes and different types, so a wrong binding is a compile error.
- **The device is a value, not a handle.** It owns the table that resolves
  handles, so a device handle would need a table to find the table.
- **Timeline fences.** `SignalFence(value)` / `WaitFence(value, timeout)`.
  D3D12 has values natively, Vulkan 1.2 has timeline semaphores, WebGPU needs an
  emulation the note records.
- **A function-pointer backend table, not virtual functions.** No RTTI, and the
  Phase 25 C ABI is a table anyway (ADR 0013).
- **The boundary is the engine's convention.** +Y up, reverse-Z 0..1, top-left
  texture origin, right-handed view, column-major matrices — with three
  predicates that tell a backend what to remap.
- **A timeout is `NotFound`, a stale handle is `InvalidArgument`.** A caller must
  be able to tell a slow GPU from a bug.

## Evidence

All commands ran on the machine named above.

| Check | Result |
|---|---|
| `scripts/build.ps1 -Preset debug` | pass |
| `scripts/build.ps1 -Preset release` | pass |
| `scripts/build.ps1 -Preset shipping` | pass |
| `scripts/build.ps1 -Preset debug-asan` | pass |
| `ctest` debug / release / shipping / debug-asan | **58/58 each** — 51 existing plus 7 new |
| `scripts/check_structure.py` | 16 modules, 1 contract |
| `scripts/layer_check.py` | 16 modules |
| `scripts/check_format.py` | 215 files |
| `scripts/verify_package.ps1` | installed consumer configures, links, runs — exit 0 |
| source consumer | configures, links, runs — exit 0 |

Two environment notes, recorded so the numbers are reproducible rather than
lucky. The MSVC ASan runtime DLL is not on `PATH` in a bare shell, so
`build/debug-asan` reports `0xc0000135` (DLL not found) for every test, including
the 51 that existed before this phase, until
`VC/Tools/MSVC/<version>/bin/Hostx64/x64` is added to `PATH`; with it, 58/58 pass.
The installed package consumer and the source consumer both link `cyber::rhi`
and exercise it (adapter enumeration, device creation, a queue, a fence
round trip, a format classification), which is what proves the new public target
is exported correctly.

New tests:

| Test | Cases | What it proves |
|---|---|---|
| `rhi_public_headers` | 1 | Every header compiles standalone; `Device` is move-only; the default device is empty and rejects every call; `NullBackend()` is stable and complete. |
| `rhi_handle` | 4 | Default invalid, equality on both fields, 64-bit pack round trip including extremes, tag types are not interconvertible. |
| `rhi_format` | 3 | Every enumerator is named; depth, sRGB, compressed, and integer classes are mutually exclusive; specific mappings hold. |
| `rhi_conventions` | 4 | Boundary constants; the backend table has a row per kind and agrees with it; unknown kinds answer the unpinned convention; queue names. |
| `rhi_capabilities` | 3 | One null adapter with a terminated name; zero-capacity enumeration returns the count; feature bitset semantics; limits are non-zero, ordered, and power-of-two aligned. |
| `rhi_device` | 6 | Create/destroy with no allocator leak; three distinct queue handles; capabilities as a value; invalid arguments; a moved-from device is empty; fences die with the device. |
| `rhi_fence` | 6 | Signal/wait, poll and bounded timeout, a real blocking wait across threads, monotonic timeline, stale-handle rejection with slot reuse, a waiter woken by destruction. |

The fence tests cross-check the allocator: `HeapAllocator::Stats().used` is zero
after a device and 64 fences are destroyed, so the null backend leaks nothing.

## Known gaps

- **No resource, pipeline, or command-list creation.** Deliberate; Phases 11-13.
- **No `DeviceLost` error code.** No null-backend path can produce one, and an
  error code with no producer is untested code. Phase 14 adds it with D3D12.
- **The null backend's fence table is O(n) in destroy**, because the free list is
  an append-only array rather than a stack. It is a validation backend; the
  allocation pattern of a real submit loop is measured in Phase 14.
- **One mutex per null device.** Creation, fencing, and capability reads
  serialize. Acceptable for validation, and Phase 15 measures submission cost.
- **The limits are the null backend's, not hardware's.** Phase 14 fills them from
  `CheckFeatureSupport` and a consistency test keeps them honest.
- **Platform, jobs, and perf are not dependencies yet.** The module declares them
  because Phase 13 needs a surface and Phase 15 needs timestamps; the CMake target
  links only `core` and `memory` today, and the layer check accepts both.

## Sources

See [the comparison notes](../notes/phase-10-api-comparison.md), which list the
D3D12 programming guide sections, the Vulkan 1.3 specification chapters, the
WebGPU candidate recommendation, Metal's clip-space reference, and Giesen's
"trip through the Graphics Pipeline 2011" part 1.

## What the next phase needs from this one

- `handle.h` is the handle rule every resource follows; Phase 11 adds tags'
  descriptors but never a second handle type.
- `capabilities.h` supplies `Limits`, which Phase 11 validation compares against.
- `format.h` is what Phase 11's numeric format table indexes; the classification
  predicates are the single source of truth for a backend's format mapping.
- `conventions.h` is what Phase 14's D3D12 backend reads instead of hard-coding
  a Y flip.
- `Device` is the only creation path, so Phase 11 extends `DeviceInterface` and
  the null backend's object table rather than adding a parallel API.
