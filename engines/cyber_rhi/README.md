# cyber_rhi

**Status:** planned · **Layer:** 2 · **Kind:** graphics

GPU hardware interface: device, queues, resources, pipelines, command lists, synchronization, swapchain, and backends.

The binding contract for this module lives in [docs/MODULE.md](docs/MODULE.md).
Read it before adding code or changing a public header.

## Layout

- `include/cyber/rhi/` — public headers, self-contained.
- `src/` — private implementation.
- `tests/` — unit tests owned by this module.

The folder exists from day one so the architecture is navigable. A CMake
target is added only when the module's phase starts.

## Roadmap

Phases 10, 11, 12, 13, 14, 15, 34, 35.
