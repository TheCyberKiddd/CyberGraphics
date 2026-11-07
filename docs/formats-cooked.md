# Cooked asset format

This specification defines the version 1 `.cymesh`, `.cytex`, `.cymat`, and
`.cyscene` files written by `cyber_assets`. The format is little-endian and
designed for checksum validation and memory-mapped, zero-copy reads.

## File header

The fixed 64-byte header is serialized field by field; native struct padding is
not part of the format.

| Offset | Type | Field | Required value or meaning |
|---:|---|---|---|
| 0 | `u32` | magic | `0x53414743` (`CGAS` bytes) |
| 4 | `u16` | format version | `1` |
| 6 | `u16` | asset kind | Mesh, texture, material, or scene |
| 8 | `u32` | header size | `64` |
| 12 | `u32` | flags | Bit 0 means little-endian; other bits are reserved |
| 16 | `u64` | asset ID | Stable ID derived from the normalized virtual path |
| 24 | `u64` | source hash | Hash of the exact source bytes |
| 32 | `u64` | settings hash | Hash of the import settings |
| 40 | `u64` | payload size | File size minus the 64-byte header |
| 48 | `u64` | payload checksum | xxHash64 of every byte after the header, seeded with `CGASCOOK` |
| 56 | `u32` | section count | Between 1 and 32 |
| 60 | `u32` | reserved | Must be zero |

Unknown versions and flags are rejected. The declared payload size must exactly
match the bytes provided, and the checksum is validated before sections are
exposed. The checksum detects accidental corruption; it is not a signature.

## Section table and payload

Immediately after the header are `sectionCount` entries, each 32 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `u32` | Section identifier |
| 4 | `u32` | Reserved; zero |
| 8 | `u64` | Absolute file offset of the section |
| 16 | `u64` | Section byte size |
| 24 | `u64` | Element count for typed sections |

The first section and every subsequent section begin at a 16-byte-aligned
absolute file offset. Padding is zero-filled and included in the checksum.
Every section must be within the file. Typed sections must have a size exactly
matching their count and element type, and their offsets must satisfy the
element alignment. String references are byte offsets and lengths into the
shared strings section.

The section table is part of the checksummed payload. Section identifiers and
record layouts are defined by the public declarations in
[`cooked.h`](../engines/cyber_assets/include/cyber/assets/cooked.h); identifiers
are stable and must not be reused. Readers reject malformed or out-of-range
sections rather than returning partial views.

| ID | Name | Element type / payload |
|---:|---|---|
| 1 | Mesh vertices | `MeshVertex` array; 96-byte canonical vertex records |
| 2 | Mesh indices | `u32` triangle-index array |
| 3 | Submeshes | `SubmeshAsset` array |
| 4 | Mesh info | One `CookedMeshInfo` record |
| 5 | Texture data | Opaque texture bytes, addressed by mip records |
| 6 | Texture levels | `TextureLevel` array |
| 7 | Material | One `CookedMaterialRecord`, then five `CookedTextureReference` records |
| 8 | Scene nodes | `CookedSceneNode` array |
| 9 | Scene instances | `CookedSceneInstance` array |
| 10 | Strings | UTF-8 bytes referenced by record offsets and lengths |
| 11 | Texture info | One `CookedTextureInfo` record |
| 12 | Scene info | One `CookedSceneInfo` record |
| 13 | Scene meshes | `AssetId` array in document order |
| 14 | Scene materials | `CookedMaterialRecord` array in document order |
| 15 | Scene material textures | Five `CookedTextureReference` records per scene material |

## Asset sections

- **Mesh:** canonical `MeshVertex` records, 32-bit triangle indices, submesh
  records, one bounds/name record, and strings.
- **Texture:** one texture-info record, mip-level records, texture bytes, and
  strings. Block-compressed data remains compressed.
- **Material:** one metallic-roughness material record, its fixed texture-slot
  references, and strings.
- **Scene:** flattened nodes and drawable instances, referenced mesh IDs,
  materials and texture references, scene metadata, and strings.

Records use the field order declared for `CookedMeshInfo`,
`CookedTextureInfo`, `CookedMaterialRecord`, `CookedTextureReference`,
`CookedSceneNode`, `CookedSceneInstance`, and `CookedSceneInfo` in the public
header. Float fields are IEEE-754 binary32. Matrices and vectors use the
repository's documented column-major convention. Boolean-like node/material
flags are 32-bit unsigned integers; reserved fields are zero. The fixed
five-slot material order is base color, metallic-roughness, normal, occlusion,
and emissive. A breaking layout change requires a format-version bump and an
architecture decision record.

## Database manifest and cache

The cooker writes an `assets.json` manifest with the `cyberassetdb` format tag.
Records contain stable IDs, normalized virtual paths, source and cooked hashes
and sizes, import settings, dependencies, and cooked paths. The cache is valid
only when the source hash and settings match and the referenced cooked file
passes header, ID, settings, and checksum validation. The manifest is a
separate JSON document; it is not embedded in a cooked payload.
