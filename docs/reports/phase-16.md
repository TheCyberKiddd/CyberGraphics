# Phase 16 report: shader language and compiler strategy

Date: 2026-11-05
Status: complete
Commits: none; all work remains uncommitted
Design note: [phase-16-shader-language.md](../design/phase-16-shader-language.md)
ADR: [0019](../adr/0019-shader-strategy.md)
Contract: [`cyber_shaders`](../../engines/cyber_shaders/docs/MODULE.md)

## What was built

- An allocator-backed compiler in `cyber_shaders` with a bounded lexer, parser,
  typed intermediate representation, type checks, and deterministic HLSL
  emission.
- Diagnostics for lexical, syntax, type, unsupported-feature, and allocation
  errors, each carrying a borrowed source name and one-based line and column.
- Golden HLSL tests, malformed and type-error tests, allocator cleanup checks,
  and a deterministic 256-input malformed-source exercise.
- The D3D12 cube comparison now compiles its vertex and pixel stages from Cyber
  shader source. The generated shaders reach the existing RHI/D3D12 compiler,
  pipeline creation, and rendered-image comparison.
- `engines/modules.json`, the module contract, build guide, module index,
  changelog, and project status pages reflect Phase 16.

## API example

```cpp
auto compiled = cyber::shaders::CompileHlsl("cube_vertex.cgs", source, allocator);
if (compiled)
{
    const cyber::memory::StringView hlsl = compiled.Value().hlsl.View();
}
else
{
    const cyber::shaders::Diagnostic& error = compiled.Error();
    // error.sourceName, error.line, error.column, error.message
}
```

The returned HLSL string is owned by the supplied allocator. The compiler has
no RHI dependency and does not compile platform bytecode.

## Validation evidence

| Command | Result |
|---|---|
| `.\scripts\build.ps1 debug -Test` | Passed, 88/88 |
| `.\scripts\build.ps1 release -Test` | Passed, 88/88 |
| `.\scripts\build.ps1 debug-asan -Test` | Passed, 88/88 |
| `.\scripts\verify_package.ps1 -Preset profile` | Passed; package consumer built and ran |
| `py -3.14 scripts\check_structure.py` | Passed, 16 modules, 1 contract |
| `py -3.14 scripts\layer_check.py` | Passed, 16 modules |
| Targeted clang-format check on changed C++ files | Passed |
| `.\scripts\format.ps1 -Check` | Passed after formatting the two Phase 15 files listed below at the owner's direction |
| `git diff --check` | Passed |

## Formatting resolution

The repository-wide formatter initially reported seven violations in the
existing Phase 15 worktree changes to
`engines/cyber_rhi/src/d3d12/d3d12_swapchain.cpp` and
`engines/cyber_rhi/tests/d3d12_device_probe.cpp`. The owner asked to fix the
Phase 15 issue; only those two files were formatted, and the full formatter
passed afterward.
