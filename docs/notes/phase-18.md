# Phase 18 reading notes — material system

Date: 2026-11-05

- glTF 2.0's metallic-roughness model supplies defaults and semantic roles for
  base color, metallic and roughness factors, alpha mode, and optional texture
  channels. The material system must preserve these meanings when serializing
  and validating values.
- glTF base-color texture RGB is color data and is sampled in sRGB; metallic-
  roughness channels are linear data. A texture reference in a material file is
  an asset URI, not an RHI texture-view handle.
- The Khronos material schemas specify factor defaults: base color `(1,1,1,1)`,
  metallic and roughness `1`, emissive `(0,0,0)`, and alpha cutoff `0.5`.
  `alphaCutoff` has a nonnegative minimum but no schema maximum, unlike the
  normalized color, metallic, roughness, emissive, and occlusion factors.
- Filament separates material definitions (model, named parameters, raster
  state, shaders) from material instances (per-surface values). This is useful
  architectural prior art; implement only the requested narrow model in-house.
- CyberGraphics has no general JSON parser and Phase 24 owns JSON/glTF
  importers. Phase 18 therefore specifies a bounded line-oriented text format
  instead of adding a dependency or prematurely implementing a generic parser.
- The RHI supports uniform buffers, bind groups, host-visible copy-source
  buffers, explicit barriers, and buffer copies. Material parameter changes can
  use per-instance staging and retained uniform buffers now; a frame ring and
  dynamic offsets remain Phase 19 work.
- GPU buffers and bind groups are not destroyed by the RHI while submissions
  still reference them. Material upload callers must order copies and graphics
  use with the existing queue/fence model and avoid modifying in-flight data.

## Sources

- Khronos, [glTF 2.0 specification — Materials](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#materials).
- Khronos, [material schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.schema.json).
- Khronos, [metallic-roughness material schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.pbrMetallicRoughness.schema.json).
- Google Filament, [Materials Guide](https://google.github.io/filament/Materials.html).
