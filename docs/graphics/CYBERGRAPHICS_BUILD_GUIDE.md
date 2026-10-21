# CyberGraphics Build Guide
Oct 20, 2026 · @The_CyberKid

## How to use this guide

This guide turns each of the 44 roadmap phases into a work package your coding agent can follow, check, and document. Every phase has tasks to tick off, official documentation to read first, tips, the files to write, and a done-when test.

## The loop for every phase

1. Read the phase's "Read first" links and write 5 to 10 lines of notes in docs/notes/phase-NN.md.
2. Write a short design note in docs/design/phase-NN-name.md before any code: what the phase builds, what it leaves out, and which interfaces it touches.
3. Work through the task checklist in small commits. Every commit builds and passes the tests.
4. Prove the done-when line, and keep the evidence (test output, screenshots, timings with the machine named).
5. Write the phase report from the template below, update CHANGELOG.md, and tag the commit phase-NN-done.
6. Record any lasting architecture decision as a short ADR (architecture decision record) in docs/adr/.

## Rules for the agent

• Ideas from docs, never code from engines. Read specifications, papers, and talks, then write the code yourself. List every source in the design note.
• Specs beat blog posts. If a tutorial and the official spec disagree, follow the spec and note the difference.
• Stop at done-when. Ask the owner before starting the next phase if a decision changes a public interface.
• Dead links: if a link has moved, search for the page title on the same site and update the link in this guide.

## Documentation layout

docs/
  README.md            index of everything below
  math-conventions.md  written before any math code
  glossary.md          terms used in code and docs
  design/              phase-NN-name.md, one per phase
  adr/                 NNNN-title.md, one per decision
  notes/               phase-NN.md, reading notes
  reports/             phase-NN.md, phase reports
CHANGELOG.md

## Phase report template

# Phase NN report: <name>

Date: YYYY-MM-DD · Commits: <first>..<last>

## What was built

## Decisions (link the ADRs)

## Evidence for done-when
Test names, screenshots, timings, and the machine they ran on.

## Known gaps

## Sources used

## What the next phase needs from this one

## Starter instruction to paste to the agent

Follow the CyberGraphics Build Guide one phase at a time. For each phase: read the listed documentation, write the design note, implement the task list in small commits, prove the done-when line, and write the phase report. Use only the C++ standard library and operating system headers. Never copy code from other engines or libraries; take ideas from specifications, papers, and talks, and list them as sources. Stop after each phase and summarize what you built and what you need decided.

## Answers to your agent's seven questions

These defaults let the agent start Phase 1 today. Change any of them before Phase 3, except the math conventions, which are the one decision that is very expensive to reverse.

#
Question
Recommended answer
Why
1
Repo placement
Standalone private repo now, added to CyberEngine as a submodule later
CyberGraphics keeps its own history, tests, and CI, and CyberEngine can pin a version
2
License
Proprietary, all rights reserved, until phase 43
Loosening a license later is easy; tightening one is not
3
Scope of first pass
Phase 1, Phase 2, and both docs, stopping at each done-when
Small steps with proof, and you review before more is built
4
Math conventions
Right-handed, +Y up, column vectors, column-major matrices, depth 0 to 1 with reverse-Z, radians, meters, linear RGB
Matches the coordinate system in the glTF 2.0 specification for importing assets, and HLSL's default matrix packing is column-major
5
CI matrix
Windows with MSVC for build and tests, plus Ubuntu with clang for sanitizers
MSVC supports AddressSanitizer but not UBSan, so UBSan and ThreadSanitizer runs need clang on Linux
6
Language subset
C++20, exceptions and RTTI off in runtime code, standard library allowed
Your rule bans third-party rendering libraries, not the standard library; swap in your own containers only where you need control (allocators, handles, hot paths)
7
Commit identity
Set the repo-local email now and fix the global one too
A malformed global address ends up on every future commit in every repo

Changing the math conventions later means touching every math call, shader, and asset importer, so lock them in before Phase 3.

## What your machine limits

• Intel HD 530: D3D12 runs, but this GPU has no hardware ray tracing and, as far as I know, no mesh shader support. Phases 31 and 32 can only be developed behind capability checks here. Expect low frame rates, and borrow or buy an NVIDIA or AMD card before the hardware test matrix in Phase 40.
• 4 CPU cores: test the job system with 1, 2, 4, and more workers than cores, since races often appear only when threads are oversubscribed.
• Software renderer: use a small test resolution (for example 640 by 360) for golden images so tests stay fast on this CPU.

## Part 1: Foundation (phases 1-5)

The foundation is everything the renderer compiles and runs on, so mistakes here get copied into every later phase.

### Phase 1: Repository and build system

Tasks
[ ] Create the repository and folder layout: include/cybergraphics/, src/, tests/, tools/, docs/, samples/, cmake/.
[ ] Write CMakePresets.json with Debug, Release, and Shipping presets for MSVC with Ninja, C++20, and warnings as errors (/W4 /WX).
[ ] Add .clang-format, .editorconfig, .gitignore, and a LICENSE file stating the chosen license.
[ ] Add a GitHub Actions workflow that builds and runs tests on every push, using a matrix so Linux jobs can be added later.
[ ] Write a one-command build script and a README with exact steps for a clean machine.
Read first: CMake presets manual · GitHub Actions job matrices
Tips
• Use presets so you and CI run identical commands.
• Keep all compiler flags in one file under cmake/, so changing a flag is a one-line edit.
• Leave the third_party/ folder out on purpose: an empty dependency list is the project's rule, and it is easier to keep than to restore.
Write down: docs/design/phase-01-build.md (commands, flags, and why), and the build section of README.md.
Done when: a clean machine builds with one command and CI is green.

### Phase 2: Core utilities

Tasks
[ ] Fixed-width type aliases and platform and build-configuration macros (CG_DEBUG, CG_RELEASE, CG_SHIPPING).
[ ] Assert and verify macros that report file and line and break into the debugger.
[ ] A thread-safe logger with levels, channels, and sinks (debugger output, console, file).
[ ] A Result<T, E> type, since exceptions are off in runtime code.
[ ] A small test runner: test registration, assertions, name filtering, a non-zero exit code on failure.
Read first: MSVC AddressSanitizer overview
Tips
• Asserts must never allocate memory, or they fail in exactly the situations you need them.
• Make the logger thread-safe now; the job system arrives in Phase 5.
• Prove the test runner works by adding one test that is meant to fail and checking the exit code.
• Run the tests under AddressSanitizer on Windows, and add UndefinedBehaviorSanitizer on Linux with clang.
Write down: docs/design/phase-02-core.md and docs/coding-standard.md (naming, error handling, no exceptions).
Done when: a failing assert prints file and line, and the runner reports pass and fail counts with a non-zero exit code on failure.

### Phase 3: Math library

Tasks
[ ] Write docs/math-conventions.md first, using the answers in the previous section.
[ ] Vec2/3/4, Mat3/4, quaternion, transform, AABB, plane, frustum, ray, color types (linear and sRGB conversion), and half floats.
[ ] Projection matrices for reverse-Z with a 0 to 1 depth range, including an infinite far plane variant.
[ ] A SIMD layer (SSE and AVX2) with a scalar fallback, and clear alignment rules.
[ ] Unit tests with a tolerance helper, and a benchmark comparing SIMD to scalar.
Read first: Depth Precision Visualized (why reverse-Z) · Scratchapixel: the projection stage
Tips
• Test the projection matrix by projecting the near and far plane corners and checking for depth 1 and 0.
• Draw the handedness and axis directions in the conventions file; a picture prevents arguments later.
• Decide how matrices are uploaded to shaders now, and write it down next to the conventions.
Write down: docs/math-conventions.md and docs/design/phase-03-math.md.
Done when: every operation has unit tests, the projection test passes, and the benchmark result is saved in the phase report.

### Phase 4: Memory and containers

Tasks
[ ] An allocator interface and linear, stack, pool, and free-list allocators, plus a per-frame allocator that resets each frame.
[ ] A tracking allocator with tags and a leak report at shutdown.
[ ] Containers: dynamic array, fixed array, open-addressing hash map, string and string view, span, and a slot map.
[ ] Generation-counted handles (index plus generation), which the RHI will use for every GPU resource.
[ ] Stress tests for all containers, run under AddressSanitizer.
Read first: Parallelizing the Naughty Dog Engine Using Fibers (the talk also covers memory allocation patterns and a frame-centric engine design)
Tips
• A stale handle that is caught by its generation number is a bug found for free, so test that case explicitly.
• Write down move and copy rules for every container before implementing them.
• Allocate through your own allocator interface everywhere in runtime code, so the leak report is complete.
Write down: docs/design/phase-04-memory.md (allocator choices and growth policy).
Done when: the leak report catches a deliberate leak, and all container tests pass under sanitizers.

### Phase 5: Platform layer and job system

Tasks
[ ] A window abstraction (create, resize, DPI, fullscreen, event queue), with all Win32 code isolated in src/platform/win32/.
[ ] Keyboard and mouse input, using raw input for mouse movement.
[ ] A time module built on the high-resolution counter, with a frame timer and a fixed-timestep helper.
[ ] File system functions: read, write, paths, memory-mapped files, and file change watching for later hot reload.
[ ] Thread, mutex, and atomic wrappers, thread naming, and a DLL loading wrapper.
[ ] A job system with a thread pool, work stealing, dependency counters, and a parallel-for helper.
Read first: Get Started with Win32 and C++ · Raw input overview · Acquiring high-resolution time stamps
Tips
• Keep window code free of renderer includes, so the platform layer can be reused by any renderer.
• Start with a plain thread pool and counters; move to fibers only if measurements show you need them.
• Search for Glenn Fiedler's article "Fix Your Timestep" before writing the fixed-timestep helper.
Write down: docs/design/phase-05-platform-jobs.md (threading rules and job dependency model).
Done when: a window opens and handles resize and fullscreen events, and 10,000 jobs scale across all cores in a test.

## Part 2: CPU software renderer (phases 6-9)

The software renderer needs no graphics API, teaches exactly what a GPU does, and becomes the reference image generator for every later backend.

### Phase 6: Framebuffer and present

Tasks
[ ] A framebuffer type with width, height, stride, and two formats (RGBA8 and RGBA32F), allocated through your allocator.
[ ] Clear, pixel get and set, rectangle fill, blit, and format conversion, including linear to sRGB at present time.
[ ] A present path that shows the buffer in the window (a Win32 DIB section is enough) and handles resizing.
[ ] Frame pacing to a 60 Hz target and an on-screen frame time readout using a tiny bitmap font of your own.
[ ] A screenshot function that writes PPM or TGA, which becomes the basis for golden images.
Read first: Get Started with Win32 and C++ · Acquiring high-resolution time stamps
Tips
• Store linear color internally and convert to sRGB only when presenting.
• Draw a gradient plus a checkerboard as the first test image; row stride bugs show up immediately in it.
• Avoid virtual calls per pixel; the inner loops run millions of times per frame.
Write down: docs/design/phase-06-framebuffer.md and a tests/images/README.md explaining how test images are produced.
Done when: the window shows an animated gradient at a steady frame rate with the frame time overlay, and two screenshots of the same frame are byte-identical.

### Phase 7: Triangle rasterization

Tasks
[ ] Line drawing as a warm-up, then an edge-function triangle fill limited to the triangle's bounding box.
[ ] Fixed-point subpixel positions and the top-left fill rule, so shared edges never leave gaps or draw a pixel twice.
[ ] Barycentric coordinates and per-vertex color interpolation.
[ ] Tile-based parallel rasterization: bin triangles into screen tiles and run one job per tile.
[ ] A coverage test that draws many triangles sharing edges and checks that every pixel is covered exactly once.
Read first: Scratchapixel: the rasterization stage · Scratchapixel: overview of the rasterization algorithm
Tips
• Use integer coverage tests; floating-point rounding is the usual cause of gaps between triangles.
• Test shared edges at awkward angles, not only axis-aligned ones.
• Keep tiles independent with no shared mutable state, so threading never changes the output.
Write down: docs/design/phase-07-raster.md (fill rule, winding order convention, fixed-point format).
Done when: 10,000 random triangles sharing edges cover every pixel exactly once, and the multithreaded renderer matches the single-threaded output bit for bit.

### Phase 8: Depth, clipping, and perspective

Tasks
[ ] The full vertex path: model, view, and projection transforms, perspective divide, and viewport transform.
[ ] Clipping against the near plane in homogeneous space (with a guard band for the other planes), producing new triangles where needed.
[ ] Back-face culling from the signed area, with a configurable winding order.
[ ] A float reverse-Z depth buffer with a greater-or-equal test, cleared to 0.
[ ] Perspective-correct interpolation of vertex attributes.
[ ] An orbit and fly camera driven by the input from Phase 5.
Read first: Scratchapixel: the visibility problem · Depth Precision Visualized
Tips
• Clip in homogeneous clip space before the perspective divide, never after.
• Render a checkerboard floor stretching to the horizon; it reveals affine versus perspective-correct interpolation at a glance.
• Test the camera moving through objects and through the near plane.
Write down: docs/design/phase-08-pipeline.md, with a diagram of the coordinate spaces from model to screen.
Done when: a rotating depth-tested cube renders without artifacts, including when the camera passes through the near plane, and unit tests cover the clipper's output.

### Phase 9: Textures, CPU shaders, and test scenes

Tasks
[ ] A texture type with a mip chain (start with a box filter), wrap modes, and nearest, bilinear, and trilinear sampling.
[ ] Shaders as C++ functions: a vertex stage that outputs a clip position and varyings, and a pixel stage that outputs a color.
[ ] Lambert and Blinn-Phong lighting as the first shaders.
[ ] PPM and TGA image loaders and an OBJ model loader (positions, normals, UVs, triangulation).
[ ] A golden-image harness: render fixed scenes at 640 by 360, compare to stored images with a per-channel tolerance, and write a difference image on failure.
Read first: PNG specification and DEFLATE, RFC 1951 (only when you add PNG loading, which Phase 24 formalizes)
Tips
• Convert sRGB textures to linear when loading, so lighting math happens in linear space.
• Store golden images as PPM at first; they are simple and easy to diff.
• Fix random seeds and keep scene definitions in code, so every test is reproducible.
Write down: docs/design/phase-09-textures-shaders.md and docs/testing-golden-images.md.
Done when: a textured, lit model renders, and five golden-image tests pass in CI.

## Part 3: RHI and Direct3D 12 (phases 10-15)

The RHI is the layer every later feature depends on, so it gets a design phase before any backend code and a null backend before the real one.

### Phase 10: RHI design

Tasks
[ ] Read the D3D12, Vulkan, and WebGPU documentation and write a comparison table of their object models (device, queues, command lists, resources, bindings) in docs/notes/phase-10-api-comparison.md.
[ ] Define the RHI objects: device, queues (graphics, compute, copy), command list, buffer, texture, sampler, shader, pipeline, binding group, fence, swapchain, all referred to by handles.
[ ] Define lifetime rules, including deferred destruction until the GPU has finished with an object.
[ ] Define threading rules: which objects are thread-safe and how command lists are recorded on several threads.
[ ] Define error handling, debug-only validation, and capability queries.
[ ] Define the conventions at the API boundary: clip-space Y direction, depth range, texture origin, and the format enum.
[ ] Write the public headers with doc comments, and a null backend that implements them.
Read first: A trip through the Graphics Pipeline 2011, part 1 (runtime, user-mode driver, kernel driver) · Direct3D 12 programming guide (sections: Understanding Direct3D 12, Work submission, Memory management) · Vulkan specification · WebGPU specification · DirectX specifications
Tips
• Design for explicit synchronization from the start, even if the first backend hides some of it.
• Keep the interface small: add a function only when a real caller needs it.
• D3D12, Vulkan, and Metal differ in details such as clip-space Y direction and depth range; write down in the design note exactly how the RHI hides each difference.
• List every public talk or document you took an idea from; copy ideas, never code.
Write down: docs/design/phase-10-rhi.md (the full design) and docs/adr/0001-rhi-object-model.md.
Done when: the headers compile, the null backend links against them, and the owner has reviewed the design document.

### Phase 11: Resources

Tasks
[ ] Buffer descriptions: size, usage flags (vertex, index, constant, storage, copy source, copy destination), and memory type (GPU only, upload, readback).
[ ] Texture descriptions: dimension (1D, 2D, 3D, cube), array size, mip levels, format, usage flags, and sample count.
[ ] A format table with bytes per pixel or block, channels, sRGB flag, depth and stencil flags, and block size for compressed formats.
[ ] Sampler descriptions: filter, address mode, anisotropy, and comparison function.
[ ] Views and subresource ranges, and a small resource state enum that backends map to D3D12 states and Vulkan layouts.
[ ] Null-backend validation that rejects invalid combinations with clear messages and detects use of destroyed handles.
Read first: Direct3D 12 programming guide (Memory management) · Texture block compression in Direct3D 11 · Programming guide for DDS
Tips
• Keep usage flags explicit; backends need them to pick heap types and layouts.
• Make the format table the single source of truth, and add a test that checks every row.
• Delay destruction by a few frames using a fence-based queue, never destroy a resource the GPU might still read.
Write down: docs/design/phase-11-resources.md and docs/formats.md (the format table).
Done when: tests for creation, lifetime, and invalid use pass on the null backend.

### Phase 12: Pipelines and binding model

Tasks
[ ] A shader module description: stage, bytecode blob, and entry point.
[ ] A graphics pipeline description: shaders, vertex layout, topology, rasterizer, depth-stencil and blend state, render target formats, and sample count; plus a compute pipeline description.
[ ] A binding model with layouts (slots with type, count, and visible stages) and groups (the actual resources), mapped to D3D12 root signatures and descriptor tables now, and Vulkan descriptor sets later.
[ ] A small per-draw constants path (root constants in D3D12, push constants in Vulkan).
[ ] A pipeline cache interface keyed by a stable hash of the description.
[ ] Validation that catches pipeline and render target format mismatches.
Read first: Direct3D 12 programming guide (resource binding) · Vulkan specification (pipelines and descriptor sets) · WebGPU specification (bind groups)
Tips
• Fix the number of binding groups and give each a change frequency (per frame, per view, per material, per draw).
• Keep description structs plain data, so they can be hashed, compared, and serialized.
• Write a test that hashes the same description twice and expects equal results.
Write down: docs/design/phase-12-pipelines-binding.md (with a table of how each binding concept maps to D3D12 and Vulkan).
Done when: every pipeline is described by plain structs and invalid combinations are rejected with clear messages, covered by tests.

### Phase 13: Command lists, synchronization, and swapchain

Tasks
[ ] A command list API: begin and end, set pipeline, bind groups, vertex and index buffers, draw, indexed draw, dispatch, copy, clear, render pass begin and end, barriers, and debug markers.
[ ] Graphics, compute, and copy queues, submission with fence signal and wait, and a ring of 2 to 3 frames in flight.
[ ] Barrier semantics: define them once, and make the null backend track resource states and validate transitions.
[ ] Swapchain create, acquire, present, resize, and present modes (vsync and tearing).
[ ] A deferred destruction queue tied to fences.
Read first: Direct3D 12 swap chains · DXGI flip model · Intel's D3D12 flip model swap chain sample · NVIDIA: swap chain best practices · Vulkan synchronization guide · Vulkan synchronization examples
Tips
• D3D12 only works with flip model swap chains, and buffer count, frames in flight, and present interval all affect latency; Intel's sample compares the combinations and fits your Intel GPU.
• In debug builds, track every resource's state and fail loudly on a mismatched transition.
• Read the Vulkan synchronization pages now even though Vulkan comes later; they sharpen your barrier design.
Write down: docs/design/phase-13-commands-sync.md, including a frame lifecycle diagram in text.
Done when: a test with a missing barrier is caught by null-backend validation, and a frame-ring test proves no resource is reused before its fence.

### Phase 14: Direct3D 12 backend

Tasks
[ ] DXGI factory and adapter enumeration with logging of adapter name, memory, and feature level; also support Microsoft's WARP software adapter so CI can run without a GPU.
[ ] Device, queues, fences, and command allocators and lists per frame.
[ ] Descriptor heaps: CPU staging heaps plus a GPU-visible ring, and descriptor allocation.
[ ] An upload ring buffer for dynamic data, and staging uploads for textures.
[ ] Shader loading using the HLSL compiler that ships with Windows for now.
[ ] Pipeline state objects and root signatures built from your binding layouts.
[ ] Milestones: a colored triangle, then a textured cube; compare both with the software renderer's images.
Read first: Direct3D 12 programming guide · Programming guide for HLSL (compiler targets) · PIX overview · DirectX specifications (enhanced barriers)
Tips
• Turn on the debug layer and GPU-based validation from the first line of device code, and treat any message as a test failure.
• Name every D3D12 object, and capture the triangle in PIX on day one, so the tool is familiar before the renderer is complex.
• Query capabilities instead of assuming them; your GPU reports feature level 12_1, and other users' GPUs will differ.
Write down: docs/design/phase-14-d3d12.md (a table mapping each RHI concept to its D3D12 equivalent) and docs/notes/hlsl-compile.md.
Done when: the triangle and textured cube render through the RHI on D3D12, and the image difference against the software reference is within a recorded tolerance.

### Phase 15: GPU memory and debug tooling

Tasks
[ ] A GPU memory allocator: large heaps, sub-allocation per memory type, aliasing for transient resources, and a statistics and fragmentation report.
[ ] Your own validation layer at the RHI level: state checks, lifetime checks, and binding completeness checks.
[ ] Object naming and event markers per pass that show up in PIX and RenderDoc.
[ ] GPU timestamp queries with per-pass timings read back through a ring buffer.
[ ] A leak report at shutdown that lists resource names.
[ ] Device-lost handling that logs diagnostic data on removal, with a test path that forces it.
Read first: Understanding the D3D12 debug layer (GPU-based validation and DRED) · PIX overview · RenderDoc documentation
Tips
• Keep a list of deliberate-error tests (wrong barrier, missing binding, use after destroy) and run it in CI to prove each one is caught.
• Make DRED output part of crash reports from the start; GPU hangs are hard to debug without it.
Write down: docs/design/phase-15-gpu-memory-debug.md and docs/debugging.md (how to use PIX and RenderDoc with CyberGraphics).
Done when: a deliberately wrong barrier is caught with a clear message, GPU pass timings are visible, and the leak report names a deliberately leaked resource.

## Part 4: Shaders and materials (phases 16-19)

Shaders are where "full control" gets expensive, so this part starts with a written decision about how much of the shader toolchain you build yourself.

### Phase 16: Shader language and compiler strategy

Tasks
[ ] Write an ADR choosing between three options: (A) HLSL as the source language, compiled with the compiler that ships with Windows; (B) your own language compiled to HLSL text; (C) your own language with your own backends for each API.
[ ] If you choose B or C: write the grammar, then a lexer, parser, and type checker, with error messages that include file, line, and column.
[ ] Define a simple intermediate representation, so later backends do not depend on the source syntax.
[ ] Write the HLSL text backend and test its output by compiling it and running the cube sample.
[ ] Add compiler tests: input to expected output, plus a set of programs that must fail with the expected error.
Read first: Reference for HLSL · Programming guide for HLSL · SPIR-V specification · SPIR-V Guide · Metal Shading Language specification
Tips
• Keep the first version of your language a thin, typed layer over HLSL concepts, and add features only when a sample needs them.
• SPIR-V is a public binary format, so writing your own SPIR-V generator later is realistic; writing D3D12's bytecode (DXIL) directly is a much larger project, so emitting HLSL text is the practical route.
• Read the Metal Shading Language specification now so the language you design can map to it later.
• Fuzz the parser: a compiler that crashes on bad input is a poor first impression.
Write down: docs/design/phase-16-shader-language.md (grammar and decisions) and docs/adr/0002-shader-strategy.md.
Done when: a small shader in your language compiles to HLSL, drives the cube sample, and reports errors with file, line, and column.

### Phase 17: Reflection, permutations, and hot reload

Tasks
[ ] Produce reflection data from your own compiler: resources, constant buffer layouts, vertex inputs, and compute thread group sizes, and use it to build RHI binding layouts automatically.
[ ] A permutation system (shader keywords) with stable key hashing, a limit on permutation count, and a report of counts per shader.
[ ] An on-disk shader cache keyed by source hash, compiler version, permutation, and target.
[ ] Compile shaders on the job system, and create pipelines asynchronously with a fallback while they compile.
[ ] Hot reload: watch shader files, recompile on change, and swap pipelines at a frame boundary.
Read first: Programming guide for HLSL (author-time versus runtime compilation) · search the Windows documentation for ReadDirectoryChangesW for file watching
Tips
• Include the compiler version in the cache key, or stale cache entries will cause confusing bugs after upgrades.
• Never block the render thread on shader compilation.
• On a compile error during hot reload, keep the last working shader and print the error.
Write down: docs/design/phase-17-shader-permutations.md.
Done when: editing a shader while the sample runs updates it within about a second, a syntax error keeps the previous shader running, and the permutation report is generated.

### Phase 18: Material system

Tasks
[ ] Material definitions: a shader, a parameter list (type, default, range), render state, and a queue (opaque, masked, transparent).
[ ] Material instances that override parameters, with parameter blocks generated from reflection and uploaded as constant buffers, plus texture slots.
[ ] Sort keys for draw ordering (queue, pipeline, material, depth).
[ ] The glTF metallic-roughness model as the first material model.
[ ] A text serialization format for materials, with parameter names and ranges stored for a future editor.
Read first: glTF 2.0 specification (materials) · Filament materials guide (a design reference for parameter sets, not code to copy)
Tips
• Keep "material" (shader plus defaults) separate from "instance" (overrides); it keeps memory use and sorting simple.
• Read how the glTF specification defines metallic-roughness before writing the shader, and test it against the glTF sample models.
• Store per-material data in one contiguous buffer region to keep uploads cheap.
Write down: docs/design/phase-18-materials.md and docs/material-format.md.
Done when: 100 materials with instances render with sorted draws, and a parameter change appears on the next frame.

### Phase 19: Constant streaming and bindless resources

Tasks
[ ] A per-frame ring buffer for constants, with the alignment rules of each API.
[ ] Per-draw constants and a structured buffer of per-object data for instancing.
[ ] A descriptor allocator with free lists and per-frame transient descriptors.
[ ] Bindless access: a global table of textures and buffers indexed in shaders, with a fallback when the GPU does not support it.
[ ] A measurement of the CPU cost of submitting 10,000 draws, recorded in the phase report.
Read first: Direct3D 12 programming guide (resource binding and descriptor heaps) · Vulkan specification (descriptor indexing)
Tips
• Query the GPU's resource binding capability instead of assuming it, and keep the fallback path tested on your Intel GPU.
• Optimize only with numbers: record the CPU time before and after each change.
Write down: docs/design/phase-19-binding-streaming.md.
Done when: 10,000 draws with unique transforms run within the frame budget, with the CPU submission cost measured and written down.

## Part 5: Renderer architecture (phases 20-25)

This part turns a working RHI into a renderer that scales to large scenes, runs on its own thread, and plugs into Cyber Engine.

### Phase 20: Render graph

Tasks
[ ] A pass declaration API with two stages: a setup stage that declares which resources a pass reads and writes, and an execute stage that records commands.
[ ] Resource handles for imported resources (like the swapchain) and transient resources, with lifetime analysis.
[ ] Automatic barrier insertion from the declared usage.
[ ] Transient resource allocation with memory aliasing, using the Phase 15 allocator.
[ ] Removal of passes whose outputs are never used.
[ ] A text dump and a Graphviz DOT export of each frame's graph for debugging.
Read first: FrameGraph: Extensible Rendering Architecture in Frostbite (GDC 2017) · the talk's slides
Tips
• A setup and execute split is what makes culling and aliasing possible, because the whole frame is known before any command is recorded.
• Test a graph with an unused branch and a diamond-shaped dependency.
• Passes should not touch the RHI outside their execute stage.
Write down: docs/design/phase-20-render-graph.md.
Done when: a depth prepass, main pass, and post-processing pass are built through the graph with automatic barriers, an unused pass is culled, and a graph dump file is produced.

### Phase 21: Render thread and frame pipelining

Tasks
[ ] Define the thread model: the game thread produces frame data, the render thread consumes it, with 2 frames in flight.
[ ] A command queue between the threads (a single-producer, single-consumer ring) and buffered render state.
[ ] Ownership rules: only the render thread touches RHI objects.
[ ] Resize and shutdown sequences that cannot deadlock.
[ ] Profiler markers that show both threads overlapping.
[ ] A single-threaded mode switch for debugging.
Read first: Parallelizing the Naughty Dog Engine Using Fibers (frame-centric design) · Direct3D 12 programming guide (Work submission)
Tips
• Keep the single-threaded mode permanently; it makes bugs far easier to reproduce.
• Run the null backend under ThreadSanitizer on Linux, since the D3D12 backend only runs on Windows.
• Stress-test resize and shutdown while frames are in flight.
Write down: docs/design/phase-21-render-thread.md.
Done when: the profiler shows the two threads overlapping, a ThreadSanitizer run on the null backend finds no races, and the resize and shutdown stress test passes.

### Phase 22: Scene, render proxies, and culling

Tasks
[ ] Render proxies (mesh instance, light, camera): the renderer's own copies of the data it needs, created, updated, and destroyed through the command queue.
[ ] Transform and bounds storage in contiguous arrays with dirty flags.
[ ] Frustum culling of bounding boxes on the job system, producing a visibility list per view.
[ ] Start with flat arrays and parallel culling, and add a BVH or grid only when measurements demand it.
[ ] LOD selection by screen-space size.
[ ] A brute-force reference culler used in tests to verify results.
Read first: there is no single authoritative source; search for "frustum culling AABB plane test", read two or three explanations, and list them in the design note.
Tips
• Culling speed is limited by memory access, so store bounds in tightly packed arrays.
• Test objects that straddle frustum planes and objects exactly on a plane.
• Measure with 100,000 objects on your 4-core machine and record the time.
Write down: docs/design/phase-22-scene-culling.md.
Done when: a 100,000-object scene is culled within a budget you set (for example 1 ms), and the results match the brute-force culler.

### Phase 23: Geometry pipeline

Tasks
[ ] Vertex formats (position, normal, tangent, two UV sets, color, skinning data), with packed variants (half floats, octahedral normals) added after the full-float versions work.
[ ] Meshes with submeshes, bounds, and 16-bit or 32-bit indices chosen by size.
[ ] Tangent generation for meshes that do not include tangents.
[ ] Your own mesh simplification for LODs (for example quadric error metrics).
[ ] Instancing and indirect draws using argument buffers.
Read first: glTF 2.0 specification (meshes, accessors, and the section on tangents) · Direct3D 12 programming guide (indirect drawing)
Tips
• Get everything correct with full floats first, and pack vertex data only afterwards.
• Check tangent handedness against a normal-mapped test model.
• Measure LOD error numerically (maximum deviation from the original) and test it.
Write down: docs/design/phase-23-geometry.md.
Done when: a scene with a million instanced triangles renders with visible LOD transitions, and the LOD error tests pass.

### Phase 24: Asset import and cooked render assets

Tasks
[ ] Your own parsers: JSON, glTF 2.0 (both .gltf and .glb, with buffers, accessors, materials, textures, and nodes), and OBJ.
[ ] Image decoders: PNG with your own inflate implementation, JPEG baseline, and readers for DDS and KTX2 containers with block-compressed payloads.
[ ] Cooked binary formats for meshes, textures, and materials, with version headers, checksums, and alignment suitable for memory mapping.
[ ] An asset database with unique IDs, dependency tracking, import settings, and a cook cache.
[ ] A command-line cooking tool, and hot reload of source assets.
[ ] Fuzz tests for every parser.
Read first: glTF 2.0 specification · PNG specification · DEFLATE, RFC 1951 · KTX 2.0 specification · Programming guide for DDS
Tips
• Treat every input file as hostile: bounds-check every offset and size before using it.
• The DDS guide lists validation rules for readers (minimum file size, magic value, and header sizes); implement them exactly.
• Cooked files should load measurably faster than source files, and a benchmark should prove it.
Write down: docs/design/phase-24-assets.md and docs/formats-cooked.md.
Done when: a glTF sample model imports, cooks, and loads measurably faster from cooked files, and each parser's fuzz test runs for a set time without a crash.

### Phase 25: Engine-facing renderer interface

Tasks
[ ] Define the API Cyber Engine calls: create and destroy scenes, views, instances, lights, and materials, submit a frame, draw debug shapes, and capture a screenshot.
[ ] A plugin interface as a stable C API with a function table, version number, and structure size fields, loaded from a DLL, with capability negotiation.
[ ] An error model across the boundary: error codes plus a message callback.
[ ] CyberGraphics on D3D12 as plugin one, and the software renderer as plugin two.
[ ] A startup setting that chooses the renderer, with automatic fallback when one is unsupported.
[ ] A compatibility test for hosts and plugins built from different versions.
Read first: your Phase 10 design document and ADR; for stable C plugin interfaces, search for "stable C ABI plugin interface design" and list the sources in the design note.
Tips
• Keep C++ types such as strings, vectors, and exceptions out of the boundary.
• Include a size and a version in every struct crossing the boundary, so new fields can be added later.
• Describe what to draw, not how to draw it; that is what lets other renderers fit.
Write down: docs/design/phase-25-engine-interface.md and docs/plugin-abi.md.
Done when: Cyber Engine switches between CyberGraphics and the software renderer with a startup setting, and the version compatibility test passes.

## Part 6: Lighting and visual quality (phases 26-31)

This is the part players actually see, and it goes well only because the architecture in Parts 3 to 5 is already solid.

### Phase 26: Physically based forward renderer

Tasks
[ ] A metallic-roughness material model with a GGX specular distribution, Smith-GGX visibility, Schlick Fresnel, and Lambert diffuse to start.
[ ] Normal mapping with tangent frames; base color textures in sRGB and data textures in linear.
[ ] Directional, point, and spot lights, with units and attenuation documented.
[ ] HDR render targets, exposure, a simple tone mapping operator, and sRGB output.
[ ] An offline reference: a small CPU path tracer that renders the same test scene for comparison.
[ ] A test scene with a grid of spheres varying roughness and metalness.
Read first: Physically Based Rendering in Filament (theory and equations; read it for ideas, do not copy code) · LearnOpenGL: PBR theory · pbr-book: microfacet models · Ray Tracing in One Weekend (for the offline reference)
Tips
• Test each BRDF term numerically, and run a white furnace test (a white sphere in a uniform white environment should not gain or lose energy).
• Keep a minimum roughness, because perfectly smooth surfaces produce zero-size highlights.
• Start with one directional light and add others once that matches the reference.
Write down: docs/design/phase-26-pbr.md (equations used and units) and docs/testing-reference-renderer.md.
Done when: the sphere grid matches the path-traced reference within a documented error metric, and the white furnace test passes.

### Phase 27: Clustered lighting and deferred option

Tasks
[ ] A structured buffer of lights uploaded each frame.
[ ] A compute pass that divides the view into a cluster grid and builds a list of lights per cluster; start with a CPU version to validate the logic.
[ ] The forward pass reads each pixel's cluster light list.
[ ] An optional deferred path with a G-buffer, so you can compare memory bandwidth and flexibility.
[ ] A rule for transparent objects (clustered forward, sorted back to front).
[ ] A debug view that shows light count per cluster as a heat map.
Read first: Clustered Deferred and Forward Shading (Olsson, Billeter, Assarsson) · Tiled and Clustered Forward Shading: Supporting Transparency and MSAA · the clustered forward rendering section of the Filament PBR document
Tips
• Measure on your Intel GPU, where memory bandwidth is limited; it will show costs a faster GPU hides.
• Forward shading handles transparency and MSAA naturally, which is why the paper's forward variant is a good default.
• Keep the CPU cluster builder as a test oracle for the compute version.
Write down: docs/design/phase-27-lighting.md.
Done when: 1,000 dynamic lights run at a stable frame time in the test scene (record the machine), and the heat map debug view works.

### Phase 28: Shadows

Tasks
[ ] A depth-only shadow pass in the render graph, using a comparison sampler that respects reverse-Z.
[ ] Cascaded shadow maps for directional lights with cascade stabilization (snapping to texels) and blending between cascades.
[ ] Spot light shadow maps and point light shadow maps (cube faces), and a shadow atlas with resolution based on importance.
[ ] Filtering with PCF, and an optional soft shadow variant.
[ ] Bias handling: constant, slope-scaled, and normal-offset, stored per light.
[ ] A cascade visualization debug mode.
Read first: the shadow mapping and point shadows chapters in the LearnOpenGL book (PDF)
Tips
• Acne and detached shadows need per-scene tuning, so expose the bias parameters and save them with each light.
• Shadow shimmering while the camera moves is fixed by snapping cascades to texel boundaries; test with a moving camera.
• Capture screenshots of the worst cases and keep them in the phase report.
Write down: docs/design/phase-28-shadows.md.
Done when: the test scene shows no acne, no detached shadows, no cascade seams, and no shimmering while moving, with screenshots in the report.

### Phase 29: Image-based lighting and reflections

Tasks
[ ] Environment map loading (equirectangular HDR to cubemap), a prefiltered specular mip chain using importance sampling, diffuse irradiance, and a BRDF lookup texture.
[ ] Reflection probes with capture, blending, and box parallax correction.
[ ] Screen-space reflections using a hierarchical depth buffer, falling back to probes.
[ ] A procedural sky first, with atmospheric scattering later.
[ ] Specular occlusion so reflections do not leak into crevices.
Read first: the image-based lighting section of the Filament PBR document · the IBL chapters in the LearnOpenGL book (PDF)
Tips
• Generate the lookup texture and probe data offline in your cooking tool, so results are deterministic.
• A uniform white environment is a free test: every smooth sphere should render white.
Write down: docs/design/phase-29-ibl-reflections.md.
Done when: a metallic sphere reflects its environment, probes blend smoothly as the camera moves, and the white environment test passes.

### Phase 30: Post-processing and anti-aliasing

Tasks
[ ] An HDR post pipeline: manual and automatic exposure, selectable tone mapping, and a color grading lookup texture.
[ ] Bloom, ambient occlusion (SSAO or GTAO), depth of field, and motion blur using a velocity buffer.
[ ] Temporal anti-aliasing with camera jitter, velocity-based reprojection, and history clamping; MSAA and FXAA as alternatives.
[ ] A per-effect toggle and GPU time readout.
Read first: the physically based camera section of the Filament PBR document (exposure) · the HDR, bloom, and SSAO chapters in the LearnOpenGL book (PDF) · for temporal anti-aliasing, search "temporal anti-aliasing history clamping" and list the sources you used
Tips
• Make every effect a render graph pass with explicit inputs, so effects can be reordered and disabled.
• Test TAA with a rotating checkerboard and thin lines, and look for ghosting.
• Add quality settings so the whole pipeline can run on your Intel GPU.
Write down: docs/design/phase-30-post-processing.md.
Done when: every effect toggles at runtime with its GPU time shown, and TAA shows no visible ghosting in the moving test scene.

### Phase 31: Global illumination and ray tracing

Tasks
[ ] Write an ADR choosing one GI approach to ship first: baked lightmaps and probes, screen-space GI, or voxel GI; compare them on static versus dynamic scenes, memory, and hardware needs.
[ ] Implement the chosen approach as render graph passes, with an offline bake tool if baked.
[ ] An optional D3D12 ray tracing path: acceleration structure builds, ray dispatch or inline ray queries, starting with ray traced shadows, behind a capability check.
[ ] A simple spatial-temporal denoiser for noisy ray tracing output.
[ ] A fallback to rasterized shadows when ray tracing is unsupported.
Read first: DirectX Raytracing (DXR) functional spec · Ray Tracing in One Weekend · pbr-book (light transport theory)
Tips
• Your Intel HD 530 cannot run ray tracing, so you need a ray-tracing-capable GPU to test that path; until then, keep it behind the capability check and unit test the logic that does not need the GPU.
• Choose the GI method by what your target games need, not by what is newest.
Write down: docs/adr/0003-gi-approach.md and docs/design/phase-31-gi-raytracing.md.
Done when: the chosen GI method runs in the test scene, and the ray tracing path switches on only on capable GPUs, verified on a second machine.

## Part 7: Scale, backends, and performance (phases 32-37)

This part makes CyberGraphics fast on large scenes and proves that the RHI really works on more than one API.

### Phase 32: GPU-driven rendering and compute

Tasks
[ ] A compute shader that culls instances and writes indirect draw arguments, with an RHI function for indirect draws (and a count-buffer variant where supported).
[ ] Hierarchical-Z occlusion culling using the previous frame's depth.
[ ] A mesh shader path where the GPU supports it, with a fallback to the normal vertex pipeline.
[ ] Use of the async compute queue for culling and light culling, with correct fences.
[ ] A test that checks CPU frame time stays flat as the object count grows.
Read first: DirectX specifications (mesh shader spec) · Direct3D 12 programming guide (indirect drawing and work submission) · PIX overview
Tips
• Verify GPU culling against the CPU culler from Phase 22 by reading back visible counts in a test.
• Async compute helps on some GPUs and not others; measure before keeping it, especially on your Intel GPU.
• Mesh shaders can only be developed behind a capability check on your current machine.
Write down: docs/design/phase-32-gpu-driven.md.
Done when: a scene with 1,000,000 objects is culled on the GPU and drawn with a handful of calls, CPU time stays flat as object count grows, and the results match the CPU culler.

### Phase 33: Texture compression and streaming

Tasks
[ ] Your own BCn decoders first (they define the correct output), then encoders for BC1, BC4, BC5, and BC7, with PSNR (peak signal-to-noise ratio) measured against the decoder.
[ ] Mip streaming with priority from screen coverage, background IO threads, and a residency manager that respects a video memory budget.
[ ] A texture pool allocator, an eviction policy, and low-resolution placeholders while data loads.
[ ] A residency debug view.
[ ] Virtual texturing only after streaming is solid.
Read first: Texture block compression in Direct3D 11 · KTX 2.0 specification · Programming guide for DDS · Direct3D 12 programming guide (Memory management)
Tips
• Stream the smallest mips first, so something is always drawn.
• Integrated GPUs share system memory with the CPU, so set budgets conservatively on your machine.
• Add PSNR thresholds to tests, so an encoder change that lowers quality fails CI.
Write down: docs/design/phase-33-compression-streaming.md.
Done when: a scene with more texture data than the memory budget runs without hitches, the residency view shows what is loaded, and the encoder PSNR tests pass.

### Phase 34: Vulkan backend

Tasks
[ ] Write an ADR deciding whether the Vulkan headers and loader count as "driver headers" under your no-libraries rule (this guide assumes they do, like d3d12.h).
[ ] Instance, device, queue selection, validation layers, and object naming.
[ ] A swapchain through the Win32 surface first, then Linux.
[ ] Memory allocation using the sub-allocation design from Phase 15.
[ ] Binding and pipeline mapping: descriptor set layouts, push constants, and dynamic rendering or render passes (decide in the ADR).
[ ] Barriers translated from your RHI states using the newer synchronization API.
[ ] Shaders as SPIR-V from your own compiler backend; if you chose option A or B in Phase 16, write the SPIR-V generator before this phase can pass.
[ ] Every sample and golden-image test running on both D3D12 and Vulkan.
Read first: Vulkan specification · Vulkan synchronization guide · Vulkan synchronization examples · LunarG: guide to synchronization validation · SPIR-V specification
Tips
• Run the validation layers, including synchronization validation, in every test, and treat messages as failures.
• Fix RHI design flaws that Vulkan exposes now; this is the cheapest moment you will ever have.
• Keep a table of behavior differences between D3D12 and Vulkan in the design note.
Write down: docs/design/phase-34-vulkan.md and docs/adr/0004-vulkan-headers.md.
Done when: every sample and golden-image test passes on both D3D12 and Vulkan using the same renderer code.

### Phase 35: Metal and other backends

Tasks
[ ] A Metal backend written in Objective-C++: device, queues, bindings, and shaders from a Metal Shading Language generator in your compiler.
[ ] A macOS window and input layer for the platform module.
[ ] An optional WebGPU backend for browsers.
[ ] A plan for consoles: apply to each platform holder's developer program, keep the RHI free of console-specific code, and expect documentation to be under NDA.
[ ] A capability matrix document listing what each backend supports.
Read first: Metal Shading Language specification · WebGPU specification
Tips
• Metal needs Apple hardware to test; GitHub Actions offers macOS runners, but check current availability and pricing.
• Keep backend-specific code in its own folder, and let the RHI tests run unchanged on every backend.
Write down: docs/design/phase-35-metal-others.md and docs/backend-capabilities.md.
Done when: Metal passes the same golden-image tests within tolerance, and console work begins only once the owner has developer program access.

### Phase 36: Performance hardening

Tasks
[ ] Define budgets: CPU time for submission and culling, GPU time per pass, system and video memory, and the allowed slowest 1% of frames.
[ ] Benchmark scenes that run automatically, record frame time percentiles as JSON, and compare against a stored baseline in CI.
[ ] Hitch prevention: precompile pipelines at load, warm the shader cache, and log every fence wait that stalls the CPU.
[ ] A profiling routine using PIX, RenderDoc, and your own timestamp queries: fix the three largest costs, then repeat.
[ ] Memory budget and fragmentation checks.
Read first: PIX overview · RenderDoc documentation · Direct3D 12 programming guide
Tips
• Benchmark on fixed hardware only, and compare medians and percentiles, not averages.
• Keep a performance log in docs/perf/ with date, machine, change, and result.
Write down: docs/design/phase-36-performance.md and the performance log.
Done when: the benchmark suite runs on every merge, and a deliberate 10% slowdown is caught by the threshold.

### Phase 37: Specialized rendering features

Tasks (order these by what your target games need; do the first two first)
[ ] 2D and UI rendering with batching, and debug line and text rendering, since every tool needs them.
[ ] GPU skinning and animated mesh rendering.
[ ] Particles (CPU first, then GPU), decals, and terrain.
[ ] Water, volumetric fog and clouds, and vegetation instancing.
Read first: glTF 2.0 specification (skins and animations); for the other features there is no single source, so search for each technique and list your sources in the design note.
Tips
• Build each feature as a render graph module that can be disabled, so it never slows scenes that do not use it.
• Give each feature its own sample scene, golden-image test, and performance budget.
Write down: one docs/design/phase-37-<feature>.md per feature.
Done when: each feature has its own sample scene, golden-image test, and performance budget written down.

## Part 8: Tools, quality, and shipping (phases 38-44)

Customers judge an engine by its tools, documentation, and stability as much as by its image quality, so this part is as important as the rendering parts.

### Phase 38: Debug UI and in-engine profiling

Tasks
[ ] Your own immediate-mode UI: draw lists of vertices and indices, a font atlas from a bitmap font to start, widgets (label, button, slider, checkbox, text input, graph), and input routing.
[ ] A debug overlay showing frame time graph, per-pass GPU timings, memory statistics, and draw and triangle counts.
[ ] Console variables with types and help text, a console window, and saved settings.
[ ] A render graph viewer built on the Phase 20 export.
[ ] A CPU profiler with scoped markers, a timeline view, and a JSON export.
Read first: PIX documentation (instrumenting an app with markers) · for immediate-mode UI design, search "immediate mode GUI" and list the explanations you used in the design note
Tips
• The debug UI must work even when the main renderer is broken, so make it render through the software backend as well.
• Do not allocate memory every frame in UI code.
• Keep the UI in its own module, separate from CyberGraphics core.
Write down: docs/design/phase-38-debug-ui.md.
Done when: the overlay shows a per-pass frame breakdown, console variables change settings at runtime, and the UI renders the same on D3D12 and the software renderer in a golden-image test.

### Phase 39: Frame capture and debugging tools

Tasks
[ ] A capture format that stores one frame's RHI commands and every resource they use (buffers, textures, shaders, pipeline descriptions), with a version header.
[ ] A replay tool that recreates the resources on any backend, replays the commands, and compares the output image with the recorded one.
[ ] An inspector listing draw calls with their state, and viewers for textures, buffers, and meshes.
[ ] Pass names and markers that show up in PIX and RenderDoc.
[ ] A hotkey and command-line option to capture a frame.
Read first: PIX documentation (including how to trigger captures from your own code) · RenderDoc documentation
Tips
• Capture at the RHI level, not the API level, so one capture can replay on any backend.
• Use stable IDs for resources, and test that replaying twice gives the same image.
Write down: docs/design/phase-39-frame-capture.md and docs/capture-format.md.
Done when: a captured frame replays on a different machine with an image difference inside the recorded tolerance.

### Phase 40: Automated testing

Tasks
[ ] Unit tests for every phase, running in CI, and golden-image tests on every backend with a stored baseline per backend and a per-backend tolerance.
[ ] A hardware matrix using machines you own as self-hosted CI runners for NVIDIA, AMD, and Intel GPUs, plus the WARP software adapter on cloud runners; record the driver version with every result.
[ ] Fuzz tests for all parsers and the shader compiler.
[ ] Stress and long-running tests: a loop of scenes for an hour, forced device removal, and rapid resizing.
[ ] Crash reporting with minidumps and a plan for symbols.
Read first: GitHub Actions job matrices · MSVC AddressSanitizer overview · MSVC /fsanitize reference (includes experimental fuzzing support) · Understanding the D3D12 debug layer
Tips
• Differences between GPU vendors are normal; record tolerances as data per backend and GPU, not as special cases in code.
• Treat flaky tests as bugs, and quarantine them with a tracking note instead of ignoring them.
• Failed image tests should attach the expected, actual, and difference images automatically.
Write down: docs/design/phase-40-testing.md and docs/ci-hardware-matrix.md.
Done when: CI runs the image tests on all three GPU vendors and attaches difference images to failures, and fuzz tests run on a schedule without crashes.

### Phase 41: Editor viewport integration

Tasks
[ ] Rendering into offscreen targets shown in editor panels, with several views that each have their own camera.
[ ] Object picking by rendering object IDs to a buffer and reading back a pixel, plus rectangle selection.
[ ] Selection outlines, translate, rotate, and scale gizmos drawn as an overlay pass, a grid, and debug shapes.
[ ] A material and mesh preview renderer that also generates thumbnails.
[ ] Integration with hot reload.
Read first: there is no single source; search for "GPU picking object ID buffer" and "transform gizmo" and list your sources in the design note.
Tips
• The editor should be a client of the Phase 25 interface; picking and gizmos should work through it, never by reaching into renderer internals.
• Pixel readback takes a few frames, so design picking to be asynchronous.
Write down: docs/design/phase-41-editor-viewport.md.
Done when: an editor viewport selects and moves objects with a gizmo, and thumbnails are generated for materials and meshes.

### Phase 42: Documentation, samples, and public API stability

Tasks
[ ] An API reference generated from header comments.
[ ] Tutorials: hello triangle, a textured model, materials and lights, post-processing, and writing a custom render pass.
[ ] At least ten samples, each built by the presets and covered by a golden-image test.
[ ] A versioning policy (semantic versioning), a deprecation policy, a changelog, and migration guides.
[ ] An audit of the public headers so no internal detail is exposed.
[ ] A fresh-eyes test with someone who has never seen the project.
Read first: search for "Semantic Versioning" and "Keep a Changelog" and adopt one clear convention; the exact format matters less than following it.
Tips
• A sample that is not tested will break; every sample should run in CI.
• Write the tutorial for someone who has not read anything in this guide.
Write down: docs/versioning-policy.md, docs/deprecation-policy.md, and the tutorials under docs/tutorials/.
Done when: a developer who has never seen the project follows the tutorial and renders a scene within an hour.

### Phase 43: Packaging, licensing, and distribution

Tasks
[ ] An SDK layout (headers, libraries, binaries, samples, docs, tools) in versioned folders, with debug symbols shipped separately.
[ ] An installer with a silent install option and a clean uninstall, and a code signing certificate for binaries and the installer.
[ ] License terms reviewed by a lawyer, and a decision between license keys and store distribution.
[ ] An auto-update mechanism that verifies signatures.
[ ] Opt-in crash reporting and telemetry, with a privacy policy.
[ ] A notices file for everything you redistribute that you did not write, including any Microsoft DLLs such as the HLSL compiler, after checking each one's redistribution terms.
[ ] Release engineering: reproducible builds, tagged releases, and an archive of every release's build outputs.
Read first: search the Microsoft documentation for the redistribution terms of every Windows file you plan to ship, and record the answers in the notices file.
Tips
• Signed binaries avoid scary warnings on Windows, so budget for a certificate early.
• Test install, update, and uninstall in a clean virtual machine, not on your development PC.
• Get the lawyer's review done before the public beta, not after.
Write down: docs/release-process.md and THIRD_PARTY_NOTICES.md (generated by a script).
Done when: install, update, and uninstall work on clean machines, the legal review is complete, and the notices file is generated by a script.

### Phase 44: Beta program and release

Tasks
[ ] A private alpha with a few developers, then a public beta, with a bug tracker, triage rules, a support channel, and a crash dashboard.
[ ] Hardware compatibility testing across GPUs and drivers, with a published known-issues list.
[ ] A release checklist covering docs, samples, license check, a security review (parsers fuzzed), and a performance report.
[ ] A support policy (which versions are supported and how often updates ship) and a post-launch roadmap.
[ ] A launch page, pricing, and a trial or demo build.
Read first: your own phase reports; this phase is about collecting what you have already documented.
Tips
• Ask alpha users to build something real, not to test features one by one; real projects find the bugs that matter.
• Keep a public known-issues list; users forgive known problems more easily than hidden ones.
Write down: docs/release-checklist.md and docs/support-policy.md.
Done when: beta users ship a test project with your engine, no blocking bugs remain, and the release candidate passes the full test matrix.
