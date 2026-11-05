# Phase 16 — Shader language and compiler strategy

**Status:** complete.

Phase 16 adds the typed Cyber shader language and HLSL source generator in
[`cyber_shaders`](../../engines/cyber_shaders/). The compiler reports lexical,
syntax, and type errors with the source name and one-based line and column.
The existing D3D12 textured-cube comparison now compiles its vertex and pixel
shaders from Cyber source and renders the same comparison image.

The source grammar and the decision to emit HLSL are documented in
[the design note](../design/phase-16-shader-language.md) and
[ADR 0019](../adr/0019-shader-strategy.md). The public API is in
[`compiler.h`](../../engines/cyber_shaders/include/cyber/shaders/compiler.h).

## Example

```cpp
auto compiled = cyber::shaders::CompileHlsl("cube_vertex.cgs", source, allocator);
if (!compiled)
{
    const cyber::shaders::Diagnostic& diagnostic = compiled.Error();
    // Report diagnostic.sourceName, diagnostic.line, diagnostic.column,
    // and diagnostic.message.
}
```

`CompiledShader::hlsl` owns its generated text through the caller-provided
allocator. The compiler does not depend on the RHI or a platform shader
compiler; D3D12 consumes the generated HLSL through the existing Windows SDK
compiler path.

## Validation and remaining gate

Debug, Release, and Debug-ASan test suites pass 88/88. The generated cube
shaders pass the D3D12 image comparison. Structure and layer checks pass, the
Profile package consumer builds and runs, and the changed Phase 16 C++ files
pass targeted clang-format checks.

The repository-wide formatter initially found seven violations in the existing
Phase 15 worktree changes to `engines/cyber_rhi/src/d3d12/d3d12_swapchain.cpp`
and `engines/cyber_rhi/tests/d3d12_device_probe.cpp`. Those two files were
formatted with the project configuration at the owner's direction, after which
`.\scripts\format.ps1 -Check` passed. No other pre-existing files were
reformatted.
