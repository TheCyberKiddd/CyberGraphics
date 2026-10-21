# Glossary

Terms are defined once here and used consistently in code, docs, and commits.

## Architecture

- **Module** — an independently buildable library under `engines/` with a
  contract, public headers, private source, and tests.
- **Engine-grade module** — a module with a stable internal interface and
  explicit ownership, even if it is not a product.
- **Product** — a module that implements the stable `cybergraphics_api` and can
  be loaded as a plugin. Currently `cyber_graphics` and
  `cyber_software_renderer`.
- **Contract** — `docs/MODULE.md`, the written promise of a module.
- **Layer** — the documentation grouping (0 foundation through 4 products).
- **Order** — the enforceable dependency rank; dependencies must have a lower
  order.
- **ADR** — architecture decision record, a short document capturing a lasting
  choice and its consequences.

## Rendering

- **RHI** — rendering hardware interface. The only layer that talks to D3D12,
  Vulkan, or Metal.
- **Backend** — an implementation of the RHI for one API, such as D3D12.
- **Device** — the GPU object that owns queues and resources.
- **Queue** — a graphics, compute, or copy submission stream.
- **Command list** — recorded GPU commands, submitted to a queue.
- **Fence** — a completion signal used for CPU/GPU and queue/queue ordering.
- **Barrier** — a resource state or layout transition.
- **Swapchain** — the chain of presentation images.
- **Render graph** — a declaration of passes and the resources they read and
  write; it derives barriers, transient allocation, and dead-pass removal.
- **Transient resource** — a resource that exists only within a frame or graph
  lifetime.
- **Bindless** — indexing textures or buffers from a global table instead of
  binding each one per draw.
- **Reverse-Z** — a depth convention where the near plane maps to 1 and the far
  plane to 0, improving floating-point precision.
- **Golden image** — a stored reference image used by an automated comparison
  test.
- **Reference renderer** — the CPU software renderer used to validate GPU
  output.

## Math

- **Column vector** — a vector treated as an `n x 1` matrix; matrices multiply
  on the left.
- **Column-major** — matrix storage with columns contiguous in memory.
- **NDC** — normalized device coordinates, after the perspective divide.
- **Clip space** — homogeneous space before the perspective divide.
- **Quaternion** — a four-component rotation representation `(x, y, z, w)`.
- **AABB** — axis-aligned bounding box.
- **Frustum** — the six-plane volume visible to a camera.

## Memory and concurrency

- **Arena / linear allocator** — bump allocation with bulk reset.
- **Pool allocator** — fixed-size blocks allocated and freed individually.
- **Free-list allocator** — variable-size blocks with a list of free regions.
- **Ring allocator** - a transient circular bump allocator; the oldest bytes may
  be reused after wraparound, so pointers do not survive a wrap.
- **Generation handle** — an index plus a generation counter; a stale handle is
  detected when the generation no longer matches.
- **Job** — a unit of work scheduled on the job system.
- **Work stealing** — idle workers take jobs from other workers' queues.
- **Frame in flight** — a frame whose GPU work may still be executing while the
  CPU prepares a later frame.
- **TSan / ASan / UBSan** — Thread, Address, and UndefinedBehavior sanitizers.

## Assets and shaders

- **Cooked asset** — a processed binary form optimized for runtime loading.
- **Reflection** — metadata produced by the shader compiler describing
  resources, constants, and vertex inputs.
- **Permutation** — a shader variant selected by keywords or compile-time
  options.
- **IR** — intermediate representation used between the shader front end and
  code generators.
- **HLSL / SPIR-V / MSL / DXIL** — shader languages and bytecode formats for
  D3D12, Vulkan, Metal, and D3D12's final bytecode.
