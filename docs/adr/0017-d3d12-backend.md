# 0017 — The Direct3D 12 backend: adapter discovery, WARP, and the increment plan

- **Status:** accepted
- **Date:** 2026-10-26
- **Deciders:** @TheCyberKiddd
- **Related:** Phase 14, [design note](../design/phase-14-d3d12.md), ADR 0013
  (RHI object model), ADR 0016 (command recording), [`cyber_rhi` contract](../../engines/cyber_rhi/docs/MODULE.md)

## Context

Phase 14 is the first backend that drives hardware, and it is the first code in
the repository that may include a graphics API header. It has to answer four
questions that outlive the phase:

1. How does the RHI learn what hardware exists, without a device and without
   global state?
2. What does CI do on a machine with no GPU?
3. How much of Phase 14 lands at once, when its done-when line needs a rendered
   image that the software renderer can be compared against?
4. Where does the debug layer sit in the build?

The build guide's tips decide part of it: the debug layer and GPU-based
validation are on from the first line of device code, and any message they emit
is a test failure. What it does not decide is the shape of adapter discovery,
which the RHI contract has already fixed as a `Backend` entry that must not
allocate.

## Decision

1. **Adapter discovery is a pure function of the machine.** `D3D12Backend()`
   returns a static, immutable `Backend`; `enumerateAdapters` creates a DXGI
   factory, walks it, releases it, and returns. There is no cached factory, no
   singleton, and nothing to shut down. The cost is one factory per call, which
   is a few hundred microseconds and is paid at startup and in tests, not per
   frame.
2. **WARP is always last.** Hardware adapters come in DXGI's preference order,
   then Microsoft's software adapter. A caller that takes index 0 takes the
   discrete GPU; a caller that takes the last index takes something that exists
   on every supported Windows version, which is what CI and the golden-image
   comparison use.
3. **The backend compiles out, not away.** `CYBER_RHI_ENABLE_D3D12` is a CMake
   option, on by default on Windows. Without it the source is not compiled,
   `CYBER_RHI_HAS_D3D12` is not defined, and `D3D12Backend()` returns null while
   `IsD3D12CompiledIn()` returns false — so a Linux build of the renderer is
   unchanged and a caller can never dereference a backend that was left out.
4. **An incomplete backend is a reported error, not a crash.** Until the device
   lands, `Backend::interface` and `createDevice` are null and `Device::Create`
   rejects the table with `InvalidArgument` before any backend code runs. A test
   pins that, so the increment boundary is visible in the suite.
5. **Phase 14 lands in ordered increments, each with its own evidence.** The
   done-when line needs a triangle and a textured cube compared against the
   software renderer, which cannot be built before resources, descriptors,
   submission, and pipelines exist on the GPU. The increments are:
   adapters (this one), device and queues, descriptors and the upload ring,
   shader compilation, pipelines from the binding model, then the two images.
   The roadmap checkbox stays unticked until the last one.
6. **The SDK's COM ownership stays local.** One small `ComOwner` template in
   the backend keeps ownership explicit and releases on every exit path. No
   third-party COM or graphics wrapper is used, and no `using namespace` escapes
   the source file.
7. **The Windows `interface` macro is dropped in the backend TU.** The SDK
   defines it for COM declarations and `DeviceCreation::interface` is an RHI
   field; the macro is undefined after the SDK headers are parsed so the RHI's
   own name survives.

## Consequences

- A caller can log every adapter, its vendor, its memory, and whether it is the
  software one before it creates anything, which is what a support report needs.
- Enumeration allocates inside DXGI but not inside the RHI: the RHI's contract
  ("must not allocate") is about the interface's own memory, and DXGI's internal
  allocation is released before the call returns.
- `driverVersion` is 0 from enumeration. DXGI exposes a UMD version only for
  D3D10/11 interfaces; reporting that as the D3D12 driver would be wrong, so the
  device fills it in when it exists.
- The adapter list is rebuilt per call, so a driver update or a hot-plugged eGPU
  is visible on the next call with no invalidation rule to get wrong.
- Compiling the backend out means a Linux CI job proves nothing about D3D12.
  The Windows job runs the same test binary, and WARP makes that possible on a
  runner with no GPU.
- Because the backend's table is null until the device lands, any consumer that
  ships between increments gets a clean error instead of a half-working device.

## Alternatives

- **A cached DXGI factory behind a mutex.** Rejected: it is global mutable state,
  it needs a teardown rule, and it saves microseconds on a call that happens at
  startup.
- **Enumerate WARP only when no hardware exists.** Rejected: the software path
  is then untested on developer machines, and it is exactly the path that must
  never rot.
- **WARP first, hardware second.** Rejected: "first adapter" would then mean the
  slowest one, and every caller would have to special-case it.
- **Land the whole phase in one commit.** Rejected: nothing would be verifiable
  until the last line, and a reviewer could not tell an adapter bug from a
  descriptor bug.
- **Vendor WARP selection through `D3D12CreateDevice(nullptr, ...)`.** Rejected:
  the RHI's contract is adapter indices, and a null-adapter shortcut would mean
  two ways to reach the same device.

## Follow-up

- The device increment adds `createDevice`/`destroyDevice`, the three queues,
  timeline fences on `ID3D12Fence`, and `Capabilities` from `CheckFeatureSupport`.
- The descriptor increment adds CPU staging heaps and a shader-visible ring, one
  per frame in flight, on the frame ring Phase 13 defined.
- Phase 15 turns the debug layer's message queue into a test failure and adds
  DRED output to the crash path.
