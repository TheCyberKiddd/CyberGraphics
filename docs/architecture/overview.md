# Architecture overview

CyberGraphics is a renderer product assembled from engine-grade modules. The
structure has one hard rule: dependencies point downward, and no module reaches
into another module's implementation.

## The shape

```text
                         +----------------------+
                         |  Cyber Engine (repo) |
                         +----------+-----------+
                                    |
                         include/cybergraphics/   stable C ABI
                                    |
              +---------------------+---------------------+
              |                                           |
     cyber_graphics (plugin)                  cyber_software_renderer (plugin)
              |                                           |
     cyber_renderer                              cyber_scene
        |      |      |                                |
     cyber_rhi  cyber_shaders  cyber_assets ----------+
        |           |              |
        +-----------+--------------+
                    |
     cyber_platform  cyber_jobs  cyber_perf  cyber_stability
                    |
          cyber_core  cyber_math  cyber_memory
```

The diagram is conceptual. The authoritative list, with layers and allowed
dependencies, is [`engines/modules.json`](../engines/modules.json), rendered to
[`engines/MODULES.md`](../engines/MODULES.md).

## What is a module?

A module is an independently buildable library with:

- a single responsibility and a written contract in `docs/MODULE.md`;
- a public include root under `include/cyber/<short>/`;
- private implementation under `src/`;
- unit tests under `tests/`;
- a declared layer, order, dependencies, and budgets;
- no dependency on a module above it.

Only two entries are products: `cyber_graphics` and
`cyber_software_renderer`. They implement the stable `cybergraphics_api`
contract and can be loaded as plugins. Everything else is a service or library.

## The dependency law

1. A module may depend only on a module with a lower `order`.
2. A module may depend on a declared contract (currently
   `cybergraphics_api`) without an order relationship.
3. No module includes another module's `src/` or private headers.
4. No module includes a backend header directly; only `cyber_rhi` may include
   D3D12, Vulkan, or Metal headers.
5. The checker in `scripts/layer_check.py` fails CI on violations.

Orders are spaced by ten so modules can be inserted without renumbering
everything. See [layers.md](layers.md).

## Data flow of a frame

The target architecture, reached in Parts 3 to 5 of the roadmap:

```text
game thread
  -> scene updates and render proxies
  -> single-producer/single-consumer command queue
render thread
  -> validate and update renderer scene
  -> cull and select LOD
  -> build render graph
  -> compile graph to RHI command lists
  -> submit to graphics/compute/copy queues
  -> present
```

The software renderer follows the same product API but draws on the CPU. That
is deliberate: if the interface only fits the GPU renderer, it is the wrong
interface.

## Ownership model

- **Game thread** owns gameplay state and produces immutable frame data.
- **Render thread** owns RHI objects and renderer-side state.
- **Worker threads** run jobs with explicit dependencies; they never touch RHI
  objects unless a job is documented as render-thread work.
- **Tools** consume public APIs only.

No global mutable state. Contexts (`Device`, `Frame`, `Allocator`,
`JobSystem`, `Renderer`) are passed explicitly.

## What is intentionally absent

- No third-party rendering libraries.
- No root `src/` junk drawer; code lives with its module.
- No God module. `cyber_graphics` composes; it does not implement rendering.
- No speculative CMake targets. A module gets a target when its phase starts.
- No game systems in this repository. Audio, physics, animation, networking,
  and gameplay belong to Cyber Engine's repository.

## Where to go next

- New to the project: read [module-contract.md](module-contract.md), then the
  contract of the module you are touching.
- Starting a phase: read [../design/README.md](../design/README.md) and write
  the design note first.
- Making a lasting decision: write an [ADR](../adr/README.md).
- Proving a phase: use the [phase report template](../reports/README.md).
