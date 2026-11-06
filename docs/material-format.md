# Cyber material text format

**Status:** Phase 18 specification, version 1

**Extension:** `.cybermat`
**Encoding:** UTF-8, no byte-order mark; LF or CRLF line endings

This specification describes immutable material definitions. Runtime texture
handles, GPU buffers, instances, and parameter overrides are not serialized.
Asset paths are opaque UTF-8 URIs for a later asset resolver.

## File structure

A file is a sequence of records, one per line. Blank lines and lines whose first
non-whitespace character is `#` are comments. Spaces and tabs delimit fields.
Quoted strings use `"..."`; the only escapes are `\"` and `\\`. A quoted value
cannot contain a newline. Numbers use decimal syntax, are finite, and are
separated by whitespace. Records are case-sensitive.

```text
CYBERMAT 1
name "PaintedMetal"
model metallic_roughness
shader "shaders/metal_vertex.cgs" "shaders/metal_pixel.cgs"
queue opaque
state cull back
state blend off
state depth_test on
state depth_write on
parameter "baseColorFactor" float4 default 1 1 1 1 range 0 0 0 0 1 1 1 1
parameter "metallicFactor" float default 1 range 0 1
parameter "roughnessFactor" float default 1 range 0 1
texture "baseColorTexture" default "textures/paint_basecolor.ktx2" color_space srgb
texture "metallicRoughnessTexture" default "textures/paint_mr.ktx2" color_space linear
end
```

## Records

- `CYBERMAT 1` is required as the first non-comment record. Other versions are
  rejected as unsupported.
- `name` is required once and contains 1–63 bytes.
- `model` is required once. Version 1 accepts only `metallic_roughness`.
- `shader` is required once and supplies vertex and pixel source URIs.
- `queue` is required once: `opaque`, `masked`, or `transparent`.
- `state` may specify `cull none|back|front`, `blend off|alpha`, and
  `depth_test on|off` / `depth_write on|off`, at most once per state property.
- `parameter` declares a unique name, a value type (`float`, `float2`, `float3`,
  or `float4`), a `default` with one finite component per lane, and a `range`
  with all minimum components followed by all maximum components. Every minimum
  must be no greater than its matching maximum; every default must lie inside
  the range.
- `texture` declares a unique slot name, a `default` URI or `none`, and a color
  role of `srgb` or `linear`. Version 1 recognizes
  `baseColorTexture`, `metallicRoughnessTexture`, `normalTexture`,
  `occlusionTexture`, and `emissiveTexture`; RGB color textures use `srgb`, data
  textures use `linear`.
- `end` is required exactly once. After it, only blank or comment lines are
  valid.

## Bounds and validation

- The complete file is at most 64 KiB, at most 256 records, and each record is
  at most 1024 bytes. Names are ASCII identifiers matching
  `[A-Za-z_][A-Za-z0-9_]{0,62}`. URIs are non-empty, at most 512 bytes, and may
  not contain control characters.
- At most 64 parameter schemas and 16 texture slots may be declared.
- Duplicate singleton records, duplicate parameter/texture names, unknown
  records, unknown fields, wrong component counts, malformed escapes, numeric
  overflow, NaN/infinity, missing required records, missing `end`, and trailing
  data are errors. No partial definition is returned.
- Parsing is independent of filesystems, GPU devices, and asset resolution.
  Diagnostics report the one-based line and column of the offending token.
- Writers emit records in the order shown above, parameters/textures in
  declaration order, normalized decimal numbers, escaped strings, LF line
  endings, and a final `end\n` record. A read/write/read round trip preserves
  the semantic definition.

## Metallic-roughness semantics

`baseColorFactor` is a linear RGBA multiplier with default `(1,1,1,1)` and
per-component range `[0,1]`. `metallicFactor` and `roughnessFactor` default to
`1` and are in `[0,1]`. `emissiveFactor` defaults to `(0,0,0)` and is in
`[0,1]` per component. `alphaCutoff` defaults to `0.5`, has a minimum of `0`,
and may use an authored upper bound above `1`; `occlusionStrength` defaults to
`1` and is in `[0,1]`.
Base-color texture RGB is sRGB color; alpha is linear. Metallic-roughness
texture channels are linear, with roughness in G and metalness in B. Optional
normal, occlusion, and emissive factors/slots follow glTF 2.0 semantics. Alpha
mode and cutoff map to the opaque/masked/transparent queue plus a parameterized
cutoff for masked materials.

## Compatibility

Version 1 is additive only within its recognized records. Incompatible grammar
or semantic changes require a format-version increment and an ADR. The runtime
material definition is created from this parsed value plus caller-resolved
pipeline and texture handles; this file is not a cooked GPU asset.
