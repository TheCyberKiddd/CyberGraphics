# Phase 17 report: reflection, permutations, and hot reload

Date: 2026-11-05
Status: complete
Commits: included in the Phase 17 implementation commit
Design note: [phase-17-shader-permutations.md](../design/phase-17-shader-permutations.md)
ADR: [0020](../adr/0020-shader-reflection-permutations-and-reload.md)
Reading notes: [phase-17.md](../notes/phase-17.md)
Contracts: [`cyber_shaders`](../../engines/cyber_shaders/docs/MODULE.md), [`cyber_renderer`](../../engines/cyber_renderer/docs/MODULE.md)

## What was built

- `cyber_shaders` now derives stage IO, resource bindings, constant-buffer
  layouts, and compute group sizes from the typed source program. HLSL reserved
  identifiers are rejected with source locations.
- Boolean shader keywords produce bounded, order-independent permutation
  identities. An on-disk cache keys source, compiler version, permutation, and
  target, and validates checksums, versions, sizes, and serialized reflection.
- `ShaderLibrary` compiles and reloads on the job system, watches shader files
  with the existing polling watcher, retains the last good version after errors,
  and writes permutation reports.
- `cyber_renderer` maps compiler reflection to RHI layouts and provides
  asynchronous graphics-pipeline builds, fallback selection, frame-boundary
  swaps, and frames-in-flight retirement.
- `samples/06_shader_hot_reload` demonstrates live TINT permutation switching,
  D3D12 rendering, valid edits, syntax-error retention, and report generation.
- Empty RHI pipeline layouts are accepted for shaders without bindings, with a
  regression test. The null-backend hot-pipeline fixture no longer enables
  depth testing without a depth attachment.

## API example

```cpp
auto compiled = cyber::shaders::CompileHlsl(sourceName, source, allocator);
if (compiled)
{
    const cyber::shaders::Reflection& reflection = compiled.Value().reflection;
    // Pass reflection to BuildReflectedLayout with the other program stages.
}
```

`ShaderLibrary::Register`, `RequestPermutation`, `Update`, `Find`, and
`WritePermutationReport` provide the reload and permutation path.
`HotGraphicsPipeline::Rebuild` schedules a pipeline build; `BeginFrame` publishes
it at a frame boundary while `Current` returns either the active pipeline or the
caller-provided fallback.

## Validation evidence

| Command | Result |
|---|---|
| `.\scripts\build.ps1 -Preset debug -Test` | Passed, 96/96 |
| `.\scripts\build.ps1 -Preset profile -Test` | Passed, 96/96 |
| `.\scripts\build.ps1 -Preset release -Test` | Passed, 96/96 |
| `.\scripts\build.ps1 -Preset shipping -Test` | Passed, 96/96 |
| `.\scripts\build.ps1 -Preset debug-asan -Test` | Passed, 96/96 |
| `.\scripts\verify_package.ps1 -Preset profile` | Passed; package consumer built and ran |
| `py -3.14 scripts\check_structure.py` | Passed, 16 modules, 1 contract |
| `py -3.14 scripts\layer_check.py` | Passed, 16 modules |
| `.\scripts\format.ps1 -Check` | Passed |
| `git diff --check` | Passed |

The live sample self-test was run with
`.\build\profile\samples\06_shader_hot_reload\cyber_sample_shader_hot_reload.exe --self-test`.
On Windows 10 Pro 22H2 with an Intel Core i5-6400 and Intel HD Graphics 530
(driver `31.0.101.2125`), the valid edit applied in 48 ms, the syntax error was
rejected in 49 ms while retaining the previous pipeline, and restore applied
in 84 ms. The permutation report was generated. This meets the one-second
done-when target on this machine; it is not a multi-vendor hardware result.

## Decisions and known gaps

Reflection remains compiler-owned; renderer code translates it to RHI
descriptors. The cache stores compiler output, not backend bytecode. The current
watcher polls rather than using `ReadDirectoryChangesW`. Storage-texture
bindings and constant buffers beyond the 128-byte `b0, space0` push-constant
block are unsupported by the reflected RHI layout path. The D3D12 sample was
validated on one adapter.
