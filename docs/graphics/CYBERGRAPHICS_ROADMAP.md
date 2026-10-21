# CyberGraphics Roadmap
Oct 19, 2026 · @The_CyberKid

## Vision and ground rules

CyberGraphics is Cyber Engine's own renderer: one C++ codebase you fully control, from math types up to the GPU backends, with no third-party rendering libraries. This roadmap has 44 phases in 8 parts, ordered so each phase gives you something that runs.

• Dependencies: the C++ standard library plus operating system headers only (Win32, D3D12, later Vulkan and Metal). Driver APIs are the one unavoidable dependency, and CyberGraphics hides them behind its own RHI.
• Design inspiration: the RHI uses top-level ideas common to modern engines (resource handles, command lists, a render graph, a render thread). No code is copied from any other engine.
• Pluggable by design: CyberGraphics is one renderer behind an engine-facing interface (phase 25), so Cyber Engine can offer other renderers later.
• Definition of done: every phase ends with something you can run and a test that proves it. Do not start a phase until the previous one's "done when" is true.
• Order matters more than speed: the foundation and the RHI design (phases 1-15) decide how much work everything after them costs. Spend your time there.

## Part 1: Foundation (phases 1-5)

The foundation is everything the renderer stands on, and it is where most of the project's long-term quality is decided.

### Phase 1: Repository and build system

• Create the CyberGraphics folder with include/, src/, tests/, tools/, docs/, and samples/.
• Set up CMake with presets for Debug, Release, and Shipping, C++20, strict warnings treated as errors, and a .clang-format file.
• Put it under git and add continuous integration that builds on every commit.
• Done when: an empty project builds with one command on a clean machine and CI is green.

### Phase 2: Core utilities

• Fixed-width type aliases, assertion macros that break into the debugger, and a logger with levels and channels.
• An error/result type, build configuration switches, and a small test runner of your own.
• Done when: a failing assert prints file and line, and the test runner reports pass/fail counts.

### Phase 3: Math library

• Vec2/3/4, Mat3/4, quaternions, transforms, AABB, plane, frustum, ray, color types, and half floats.
• SIMD wrappers (SSE/AVX first), with a scalar fallback.
• Write down the conventions once in docs/math-conventions.md: handedness, row or column vectors, depth range 0 to 1, and reverse-Z. Changing these later is very costly.
• Done when: every operation has unit tests and one benchmark compares SIMD against scalar.

### Phase 4: Memory and containers

• An allocator interface with linear, stack, pool, free-list, and ring allocators, plus a tracking allocator that reports leaks.
• Dynamic array, hash map, string and string view, span, and generation-counted handles.
• Done when: leak reports work and all containers pass tests under address and undefined-behavior sanitizers.

### Phase 5: Platform layer and job system

• Window creation, event loop, keyboard and mouse input, a high-resolution timer, file I/O, DLL loading, and thread/atomic wrappers.
• A job system with a thread pool and work-stealing queues, plus profiler markers.
• Done when: a window opens with resize and fullscreen events, and 10,000 jobs scale across all cores.

## Part 2: CPU software renderer (phases 6-9)

A software renderer is the one part of CyberGraphics that needs no graphics API, and it teaches you exactly what the GPU does. It later becomes your reference renderer for checking GPU output.

### Phase 6: Framebuffer and present

• A pixel buffer (8-bit RGBA and float formats), clear and blit functions, and presenting the buffer to the window through the OS.
• Frame pacing and an on-screen frame time readout.
• Done when: the window shows an animated gradient at a steady frame rate.

### Phase 7: Triangle rasterization

• Line drawing, then an edge-function triangle rasterizer with the top-left fill rule, subpixel precision, and per-vertex color interpolation.
• Split the screen into tiles and rasterize them in parallel on the job system.
• Done when: a test image of thousands of triangles has no gaps or double-drawn pixels along shared edges.

### Phase 8: Depth, clipping, and perspective

• The full vertex path: model, view, and projection transforms, then clipping against the near plane in homogeneous space, back-face culling, and a reverse-Z depth buffer.
• Perspective-correct interpolation of vertex attributes.
• Done when: a rotating depth-tested cube renders with no artifacts, including when the camera moves through the near plane.

### Phase 9: Textures, CPU shaders, and test scenes

• Texture sampling with nearest, bilinear, mipmaps, and trilinear filtering.
• Vertex and pixel shaders written as C++ functions, with simple lighting.
• Your own image loaders (start with TGA or PPM) and an OBJ model loader.
• Save golden reference images for a set of test scenes.
• Done when: a textured, lit model renders and five golden-image tests pass in CI.

## Part 3: RHI and Direct3D 12 (phases 10-15)

The RHI (rendering hardware interface) is the layer every other renderer feature depends on, so it gets a design phase before any backend code.

### Phase 10: RHI design

• Write a design document covering the object model (device, queues, command lists, resources), explicit synchronization, handle-based resources with generation counters, capability queries, threading rules, and error handling.
• Read the public documentation for D3D12, Vulkan, and Metal to find the common subset, and decide which features are optional.
• Done when: the interface headers compile and a null backend (does nothing) links against them.

### Phase 11: Resources

• Buffers (vertex, index, constant, structured, upload, readback), textures (1D, 2D, 3D, cube, arrays), samplers, views, and a format table.
• A resource state model that works the same across APIs.
• Done when: the null backend passes tests for creation, lifetime, and invalid-use detection.

### Phase 12: Pipelines and binding model

• Graphics and compute pipeline descriptions: shaders, vertex layout, blend, depth, and rasterizer state.
• A binding model (groups of resources bound together) that maps to D3D12 root signatures and Vulkan descriptor sets.
• A pipeline cache.
• Done when: every pipeline is described by plain structs and invalid combinations are rejected with clear messages.

### Phase 13: Command lists, synchronization, swapchain

• Command list recording for graphics, compute, and copy queues, plus fences, barriers, and multiple frames in flight.
• Swapchain creation, resizing, and present modes.
• Done when: the null backend's validation catches a missing barrier in a test.

### Phase 14: Direct3D 12 backend

• Adapter selection, device, swapchain, descriptor heaps, command allocators, an upload ring buffer, and pipeline creation.
• Use the compiler that ships with Windows (d3dcompiler) for shaders for now; your own shader pipeline comes in Part 4.
• Done when: a colored triangle, then a textured rotating cube, render through the RHI on D3D12 and match the software renderer's image within a tolerance.

### Phase 15: GPU memory and debug tooling

• A GPU memory allocator with sub-allocation, heap management, and aliasing.
• Your own validation layer (barrier and lifetime checks), object naming, GPU markers, timestamp queries, a resource leak report, and device-lost handling.
• Done when: a deliberately wrong barrier is caught with a clear message, and GPU pass timings are visible.

## Part 4: Shaders and materials (phases 16-19)

Shaders are where "full control" gets expensive, so this part starts with a decision about how much of the shader toolchain you write yourself.

### Phase 16: Shader language and compiler strategy

• Decide the approach: your own shader language with your own compiler (lexer, parser, type checker, intermediate representation), then one code generator per backend.
• Be realistic about output: generating HLSL or MSL text is straightforward, and SPIR-V is a public binary format you can write yourself. Writing DXIL (D3D12's bytecode) directly is the hardest target, so the pragmatic route is to emit HLSL and use the compiler Windows provides.
• Done when: a small shader in your language compiles to HLSL, drives the cube sample, and reports errors with file and line numbers.

### Phase 17: Reflection, permutations, and hot reload

• Derive resource bindings from your own compiler instead of parsing backend output.
• A permutation system (shader keywords), an on-disk shader cache, and compilation on the job system.
• Hot reload when a shader file changes.
• Done when: editing a shader while the sample runs updates it within about a second, and a report shows permutation counts per shader.

### Phase 18: Material system

• Material definitions (shader, parameters, render state) and material instances that override parameters.
• Parameter blocks mapped to constant buffers, texture slots, and sort keys for draw ordering.
• Done when: 100 materials with instances render with sorted draws, and a parameter change shows up live.

### Phase 19: Constant streaming and bindless resources

• A per-frame ring buffer for constants, dynamic offsets, descriptor management, and bindless access to textures and buffers.
• Done when: 10,000 draws with unique transforms run within the frame budget, with the CPU cost of submission measured and recorded.

## Part 5: Renderer architecture (phases 20-25)

This part turns a working RHI into a renderer that can scale to complex scenes, run on its own thread, and plug into Cyber Engine.

### Phase 20: Render graph

• Declare passes and the resources they read and write; the graph handles transient resource allocation, memory aliasing, barriers, and removes passes whose output is never used.
• A text dump and a visualizer of the graph for debugging.
• Done when: a multi-pass frame (depth prepass, main pass, post-processing) is built through the graph with automatic barriers, and an unused pass is culled.

### Phase 21: Render thread and frame pipelining

• Separate game thread and render thread, with a command queue between them and buffered render state so neither waits on the other.
• Control over how many frames are in flight.
• Done when: the profiler shows both threads overlapping and a thread sanitizer run finds no data races.

### Phase 22: Scene, render proxies, and culling

• A renderer-side scene made of proxies (copies of the data the renderer needs), updated from the game thread through the command queue.
• Parallel frustum culling on the job system, a spatial structure (BVH or grid), and LOD selection.
• Done when: a 100,000-object scene culls within a budget you set (for example 1 ms) and the result is verified against a brute-force check.

### Phase 23: Geometry pipeline

• Vertex formats and packing, meshes with submeshes, tangent generation, and bounds.
• Your own LOD generation (mesh simplification), instancing, and indirect draws.
• Done when: a scene with a million instanced triangles renders with visible LOD transitions.

### Phase 24: Asset import and cooked render assets

• Your own parsers: glTF (including a JSON parser), OBJ, PNG and JPEG decoders, and DDS or KTX texture containers.
• Cooked binary formats for meshes, textures, and materials, an asset database with unique IDs, an offline cooking tool, and hot reload.
• Done when: a glTF sample model imports, cooks, and loads measurably faster from the cooked files than from the source.

### Phase 25: Engine-facing renderer interface

• Define the API Cyber Engine calls: scene updates, views, lights, materials, and debug drawing.
• Make renderers loadable as plugins through a stable, versioned C interface, with capability negotiation.
• Keep the software renderer as the second implementation; it proves the interface is not tied to one design.
• Done when: Cyber Engine switches between CyberGraphics and the software renderer with a startup setting.

## Part 6: Lighting and visual quality (phases 26-31)

This is the part players actually see, and it only goes well once the architecture in Parts 3 to 5 is solid.

### Phase 26: Physically based forward renderer

• A metallic-roughness material model with a GGX specular BRDF, Fresnel, and Smith shadowing, plus normal mapping.
• Directional, point, and spot lights, rendering in linear color to HDR targets, and basic tone mapping.
• Done when: a grid of spheres varying roughness and metalness looks correct and matches an offline reference render you write.

### Phase 27: Clustered lighting and deferred option

• Light culling in a compute shader (clustered forward), plus an optional deferred path with a G-buffer.
• Choose the default based on measurements, and decide how transparent objects are handled.
• Done when: 1,000 dynamic lights run at a stable frame time in a test scene.

### Phase 28: Shadows

• Cascaded shadow maps for directional lights, shadow maps for spot and point lights, and a shadow atlas.
• Filtering (PCF and a soft-shadow variant), bias handling, and cascade stabilization.
• Done when: the test scene shows no shadow acne, no detached shadows, and no visible seams between cascades.

### Phase 29: Image-based lighting and reflections

• Environment maps, prefiltered specular, diffuse irradiance, and a BRDF lookup texture.
• Reflection probes with blending, screen-space reflections, and a sky model.
• Done when: a metallic sphere reflects its environment correctly and probes blend smoothly as the camera moves.

### Phase 30: Post-processing and anti-aliasing

• Auto exposure, tone mapping, bloom, ambient occlusion (SSAO or GTAO), depth of field, motion blur, and color grading.
• Temporal anti-aliasing, with MSAA or FXAA as alternatives.
• Done when: every effect toggles at runtime with its GPU time shown, and TAA shows no ghosting in a moving test scene.

### Phase 31: Global illumination and ray tracing

• Pick one approach to ship first (baked lightmaps and probes, screen-space GI, or voxel GI), since each is a large project on its own.
• Add hardware ray tracing through D3D12's ray tracing support as an optional path for shadows, reflections, or GI, with a denoiser.
• Done when: the chosen GI method runs in the test scene, and the ray traced path switches on only on GPUs that support it.

## Part 7: Scale, backends, and performance (phases 32-37)

This part makes CyberGraphics fast on large scenes and proves the RHI works on more than one API.

### Phase 32: GPU-driven rendering and compute

• GPU-side culling, indirect multi-draw, mesh shaders where supported, and async compute.
• Done when: a scene with 1,000,000 objects is culled on the GPU and drawn with a handful of calls, and CPU time stays flat as object count grows.

### Phase 33: Texture compression and streaming

• Your own BCn texture compressor, mip streaming, a residency manager with a memory budget, and background loading threads.
• Virtual texturing is optional and best left until streaming works well.
• Done when: a scene with more texture data than the video memory budget runs without hitches, and a residency view shows what is loaded.

### Phase 34: Vulkan backend

• The second backend is the real test of the RHI: swapchain, synchronization, descriptor indexing, and validation layers, plus Linux and Android window support.
• Fix RHI design flaws this exposes now, while it is still cheap.
• Done when: every sample and golden-image test passes on both D3D12 and Vulkan using the same renderer code.

### Phase 35: Metal and other backends

• Metal for macOS and iOS (needs Objective-C++ glue code), and WebGPU if you want browser support.
• Console backends are only possible after you are accepted into each platform holder's developer program, and their documentation is under NDA.
• Done when: Metal passes the same golden-image tests, and console work starts only once access is granted.

### Phase 36: Performance hardening

• Set CPU, GPU, and memory budgets, remove pipeline stalls, precompile pipelines to avoid first-use hitches, and track frame time consistency (the slowest 1% of frames), not only the average.
• Benchmark scenes that run in CI and flag regressions against a threshold you choose.
• Done when: the benchmark suite runs on every merge and a deliberate slowdown is caught.

### Phase 37: Specialized rendering features

• GPU skinning for animated meshes, particles, decals, terrain, water, volumetric fog and clouds, vegetation, 2D and UI rendering, and debug lines and text.
• Order these by what your target games need first.
• Done when: each feature has its own sample scene, golden-image test, and performance budget.

## Part 8: Tools, quality, and shipping (phases 38-44)

Selling the engine means customers judge it by its tools, documentation, and stability as much as by its image quality.

### Phase 38: Debug UI and in-engine profiling

• Your own immediate-mode UI drawn through CyberGraphics, a debug overlay, CPU and GPU timelines, console variables, and a render graph viewer.
• Done when: the overlay shows a per-pass frame breakdown and console variables change settings at runtime.

### Phase 39: Frame capture and debugging tools

• Record all commands and resources of one frame to a file, replay it, and inspect draw calls, buffers, and textures.
• External tools such as PIX and RenderDoc keep working alongside this, so make sure names and markers show up in them.
• Done when: a captured frame replays identically on another machine.

### Phase 40: Automated testing

• Golden-image comparison across backends with tolerances, importer fuzzing, stress and long-running tests, and crash reporting.
• A hardware test matrix covering NVIDIA, AMD, and Intel GPUs.
• Done when: CI runs the image tests on all three vendors and reports differences as images.

### Phase 41: Editor viewport integration

• Rendering into editor panels, multiple views, object picking through an ID buffer, selection outlines, gizmos, material previews, and thumbnails.
• Done when: an editor viewport supports selecting and moving objects with a gizmo.

### Phase 42: Documentation, samples, and public API stability

• An API reference, tutorials, at least ten sample projects, a versioning and deprecation policy, and a changelog.
• Done when: a developer who has never seen the project follows the tutorial and renders a scene within an hour.

### Phase 43: Packaging, licensing, and distribution

• An installer and SDK layout, license terms, license key or store distribution, auto-update, and opt-in crash reporting.
• A legal review of your license agreement and a notices file for anything you ship that you did not write.
• Done when: install and update both work on a clean machine and the legal review is complete.

### Phase 44: Beta program and release

• A private alpha, then a public beta, with a bug tracker, a support channel, and hardware compatibility testing.
• A launch checklist and a post-launch roadmap.
• Done when: beta users ship a test project with your engine, no blocking bugs remain, and the release candidate passes the full test matrix.

## Risks and what to do this week

The biggest risk is scope: this roadmap is years of work, so protect your ability to ship something usable early.

Risk
Why it matters
What to do
Scope growth
44 phases is a lot of work for a small team
Treat phases 1-26 as the first sellable core; everything after is expansion
RHI design mistakes
Every later phase builds on it
Keep it small, and write the Vulkan backend (phase 34) as soon as D3D12 works if you can
Shader toolchain cost
A compiler is a project of its own
Emit HLSL text first and delay direct bytecode generation
Hardware variety
Bugs often appear on only one GPU vendor
Build the three-vendor test matrix (phase 40) early, not at the end
Legal and licensing
Mistakes are expensive after launch
Get a lawyer's review of your license and distribution terms before phase 43

This week, starting from an empty folder

1. Create the repository and folder layout, and get an empty project building with CMake (phase 1).
2. Add the assert, logging, and test runner code (phase 2).
3. Write docs/math-conventions.md before writing any math code (phase 3).
4. Write a one-page requirements note: which platforms ship first, and what kind of games Cyber Engine should be best at. Every later phase gets easier once this is decided.
