# cyber_rhi

**Status:** active · **Layer:** 2 · **Kind:** graphics

GPU hardware interface: device, queues, resources, pipelines, command lists, synchronization, swapchain, and backends.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/rhi/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

## Phase 10 state

Phase 10 defines the object model and the backend boundary:

- Eleven generation-counted handle types (`handle.h`), covering queues, command
  lists, buffers, textures, views, samplers, shaders, pipelines, binding groups,
  fences, and swapchains.
- A `Device` that owns them, exposes one queue per class, and implements
  monotonic 64-bit timeline fences.
- A backend as a table of function pointers (`backend.h`) with no virtual
  functions and no RTTI, plus the `NullBackend()` validation backend.
- The boundary conventions (+Y up, reverse-Z in 0..1, top-left texture origin)
  with a per-backend mapping table in `conventions.h`.

Resource, pipeline, and command-list creation arrive in Phases 11 to 13. The
design is in [docs/design/phase-10-rhi.md](../../docs/design/phase-10-rhi.md) and
[ADR 0013](../../docs/adr/0013-rhi-object-model.md).

## Roadmap

Phases 10, 11, 12, 13, 14, 15, 34, 35.
