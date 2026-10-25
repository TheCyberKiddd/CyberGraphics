# cyber_rhi — module contract

**Kind:** graphics
**Layer:** 2
**Order:** 20
**Status:** active
**Owner:** @TheCyberKiddd

## Purpose

GPU hardware interface: device, queues, resources, pipelines, command lists, synchronization, swapchain, and backends.

## Public API

`include/cyber/rhi/`

Only headers under this path are public. Everything under `src/` is private.
Public headers must compile on their own and must not include another module's
private headers.

| Header | Contents |
|---|---|
| `handle.h` | `Handle<Tag>` aliases for every object type, validity, `PackHandle`/`UnpackHandle`. |
| `format.h` | `Format`, `FormatName`, depth/stencil, sRGB, block-compressed, and integer classification. |
| `conventions.h` | Boundary constants (+Y up, reverse-Z 0..1, top-left origin) and the per-backend mapping predicates. |
| `capabilities.h` | `BackendKind`, `AdapterInfo`, `Limits`, `Feature`, `Features`, `Capabilities`. |
| `descriptors.h` | `QueueKind`, `ValidationLevel`, `DeviceDesc`, `FenceDesc`, `SurfaceDesc`. |
| `backend.h` | `Backend`, `DeviceInterface`, `DeviceCreation`, `EnumerateAdapters`, `NullBackend`. |
| `device.h` | `Device` with queues, timeline fences, and `WaitIdle`. |
| `rhi.h` | Umbrella include. |

## Dependencies

Allowed: cyber_core, cyber_math, cyber_memory, cyber_platform, cyber_jobs, cyber_perf, cyber_stability

Forbidden: cyber_renderer, cyber_graphics, cyber_ui

Dependencies are one-way. A module may depend only on modules with a lower
`order` value, or on a declared contract such as `cybergraphics_api`.

## Invariants

- Only this module includes D3D12, Vulkan, or Metal headers.
- Synchronization is explicit; there are no hidden barriers.
- Resources use generation-counted handles and deferred destruction.
- The null backend validates every public operation.

## Non-goals

- Render graph, materials, lighting, and passes (cyber_renderer).
- Window creation (cyber_platform).

## Tests

Null-backend creation, lifetime and invalid-use tests; barrier validation; backend conformance suite.

Phase 10 delivers `rhi_public_headers`, `rhi_handle`, `rhi_format`,
`rhi_conventions`, `rhi_capabilities`, `rhi_device`, and `rhi_fence`.

## Budgets

CPU submission cost measured. No per-draw heap allocation.

## Roadmap

Phases 10, 11, 12, 13, 14, 15, 34, 35.
