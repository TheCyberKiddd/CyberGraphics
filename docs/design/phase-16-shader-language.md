# Phase 16 — Shader language and compiler strategy

Date: 2026-11-05
Status: accepted design
Related: [ADR 0019](../adr/0019-shader-strategy.md), [cyber_shaders contract](../../engines/cyber_shaders/docs/MODULE.md)

## Goal

Add a small, typed Cyber shader language that compiles to HLSL, then use its
generated vertex and pixel shaders in the existing D3D12 cube comparison. Keep
the language deliberately limited to what this sample needs; later phases add
reflection, permutations, caching, and hot reload.

## Decision

Use a standalone compiler in `cyber_shaders`. Its public boundary accepts UTF-8
source and an explicit allocator and returns owned HLSL text, the shader stage,
or a diagnostic with the source name, line, and column. The module does not
depend on the RHI or a platform shader compiler. Generated HLSL remains plain
text and is passed to the existing RHI shader path, which uses the Windows SDK's
`D3DCompile` on D3D12.

The compiler is a sequence of bounded stages:

1. Lex source into borrowed token spans carrying line and column.
2. Parse declarations and expressions into a typed intermediate representation.
3. Type-check names, stage inputs/outputs, assignments, constructors, matrix
   multiplication, texture sampling, and return values.
4. Emit deterministic HLSL for the selected stage.

Every allocation uses the caller-provided allocator. Malformed or oversized
input returns a diagnostic; it must not assert, throw, read outside the source,
or silently emit partial HLSL.

## Language v1

One source unit describes one stage:

```text
shader vertex Cube {
    input float3 position @location(0);
    input float2 uv @location(1);
    output float4 clipPosition @builtin(position);
    output float2 texcoord @location(0);
    uniform float4x4 modelViewProjection @register(b0);

    output.clipPosition = mul(modelViewProjection, float4(input.position, 1.0));
    output.texcoord = input.uv;
}
```

The pixel stage supports typed inputs, a texture and sampler binding, and a
typed return expression:

```text
shader pixel Cube {
    input float4 position @builtin(position);
    input float2 uv @location(0);
    texture2d albedo @register(t0, space0);
    sampler albedoSampler @register(s0, space0);

    return sample(albedo, albedoSampler, input.uv);
}
```

The v1 grammar includes identifiers, scalar/vector/matrix types, `texture2d`,
`sampler`, integer binding attributes, stage IO declarations, uniform
declarations, assignments, `return`, member access, numeric literals,
parentheses, constructors, and calls to `mul` and `sample`. Operators,
control-flow, user-defined functions, arrays, structs, macros, includes, and
preprocessor directives are out of scope. Unknown syntax and unsupported types
are errors with a precise source location.

Vertex HLSL uses `TEXCOORD` semantics for location attributes and
`SV_Position` for the position builtin. The v1 pixel input includes the
`position` builtin as `SV_Position`, as required by the existing D3D12 cube
pipeline, and user inputs use matching `TEXCOORD` locations. Pixel return values
map to `SV_Target`. Uniforms become an HLSL constant buffer; texture and sampler
declarations preserve their requested registers. The generated entry point is
`main`.

## Integration and validation

Replace the inline HLSL for the cube in `tests/integration/backend_compare`
with compiler output for both stages. This makes the generated code reach the
existing D3D12 compilation, pipeline creation, and rendered-image comparison,
without coupling `cyber_shaders` to `cyber_rhi` or changing the CPU reference
renderer.

Tests cover deterministic golden output, valid cube stages, location and
register emission, wrong-stage declarations, unknown names, type mismatches,
malformed tokens, truncated input, and source-location accuracy. The Windows
D3D12 integration comparison proves generated HLSL compiles and drives the
existing cube draw. No external shader compiler or runtime dependency is added.

## Non-goals

- Reflection, permutation management, shader cache, background compilation, or
  hot reload (Phase 17).
- Materials or material instances (Phase 18).
- Direct DXIL, SPIR-V, or MSL generation.
- General-purpose programming language features or shader optimization.
- Changes to the stable C ABI.
