# Phase 17 — Reflection, permutations, and hot reload

**Status:** complete.

Phase 17 adds compiler-owned shader reflection, stable keyword permutations,
an on-disk compiler-output cache, asynchronous shader compilation and reload,
and renderer-side asynchronous graphics-pipeline creation with frame-boundary
publication and deferred retirement. The D3D12 hot-reload sample demonstrates
the end-to-end path.

The compiler reflects stage IO, resources, constant-buffer member layouts, and
compute thread-group sizes from the typed Cyber shader program. Permutations
are bounded to eight boolean keywords per shader and have order-independent
hashes. Cache entries are checksummed, versioned, and validated as hostile
input. `ShaderLibrary` uses the job system and polling file watcher; failed
compiles retain the last good version. `cyber_renderer` builds RHI layouts from
reflection and swaps successful pipelines only at `BeginFrame`.

The D3D12 sample is [`06_shader_hot_reload`](../../samples/06_shader_hot_reload/).
Its automated live self-test applied a valid edit in 48 ms, rejected a syntax
error in 49 ms without changing the active pipeline, and applied the restored
shader in 84 ms on the current Intel HD Graphics 530 machine.

See the [design note](../design/phase-17-shader-permutations.md),
[ADR 0020](../adr/0020-shader-reflection-permutations-and-reload.md), and
[phase report](../reports/phase-17.md) for the API, decisions, validation, and
known gaps.

## Validation

Debug, Profile, Release, Shipping, and Debug-ASan builds each pass 96/96 tests.
The Profile package consumer, structure/layer checks, formatter, and
`git diff --check` pass. The D3D12 self-test passes the one-second reload target
and verifies the last-good-pipeline behavior.

## Known gaps

- The cache stores generated HLSL and reflection, not backend bytecode. The RHI
  has no bytecode-export API yet.
- The watcher polls timestamps rather than using directory notifications.
- Renderer layouts currently reject storage textures and constant buffers other
  than the `b0, space0` push-constant block (up to 128 bytes).
- The phase's D3D12 sample evidence is from one Intel adapter; it is not a
  hardware compatibility matrix.
