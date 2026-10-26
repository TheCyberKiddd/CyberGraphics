# Format table

Every `cyber::rhi::Format` enumerator with its numeric row. The header is the
source of truth — `FormatInfoOf` in
[`format.h`](../engines/cyber_rhi/include/cyber/rhi/format.h) — and the test
`rhi_format_table` walks the enumeration and checks this table against it, so a
row added to one and not the other fails the build.

## Columns

- **Bytes/block** — bytes for one texel, or for one whole block when the format
  is block compressed.
- **Block** — texels per block in each direction: 1x1 for uncompressed formats,
  4x4 for BC formats. A texture extent is in texels; the byte size rounds up to
  whole blocks.
- **Channels** — channels stored, counting depth and stencil as channels. BC1
  counts four channels because the fourth is interpolated from the endpoint
  pair, not stored per texel.
- **Type** — `unorm` (unsigned normalized integer), `float`, `uint`, `depth`,
  or `depth_stencil`.
- **Flags** — `srgb` when reads decode and writes encode through the sRGB
  transfer function; `compressed` for BC; `depth` and `stencil` for the depth
  formats.

## Color

| Format | Bytes/block | Block | Channels | Type | sRGB |
|---|---:|---:|---:|---|---|
| `R8Unorm` | 1 | 1x1 | 1 | unorm | no |
| `RG8Unorm` | 2 | 1x1 | 2 | unorm | no |
| `RGBA8Unorm` | 4 | 1x1 | 4 | unorm | no |
| `RGBA8UnormSrgb` | 4 | 1x1 | 4 | unorm | yes |
| `BGRA8Unorm` | 4 | 1x1 | 4 | unorm | no |
| `BGRA8UnormSrgb` | 4 | 1x1 | 4 | unorm | yes |
| `R16Float` | 2 | 1x1 | 1 | float | no |
| `RG16Float` | 4 | 1x1 | 2 | float | no |
| `RGBA16Float` | 8 | 1x1 | 4 | float | no |
| `R32Float` | 4 | 1x1 | 1 | float | no |
| `RG32Float` | 8 | 1x1 | 2 | float | no |
| `RGBA32Float` | 16 | 1x1 | 4 | float | no |
| `R32Uint` | 4 | 1x1 | 1 | uint | no |
| `RG32Uint` | 8 | 1x1 | 2 | uint | no |
| `RGBA32Uint` | 16 | 1x1 | 4 | uint | no |
| `R10G10B10A2Unorm` | 4 | 1x1 | 4 | unorm | no |
| `RG11B10Float` | 4 | 1x1 | 3 | float | no |

## Depth and depth-stencil

| Format | Bytes/block | Block | Channels | Type | Stencil |
|---|---:|---:|---:|---|---|
| `D16Unorm` | 2 | 1x1 | 1 | depth | no |
| `D32Float` | 4 | 1x1 | 1 | depth | no |
| `D24UnormS8Uint` | 4 | 1x1 | 2 | depth_stencil | yes |
| `D32FloatS8Uint` | 8 | 1x1 | 2 | depth_stencil | yes |

## Block compressed

| Format | Bytes/block | Block | Channels | Type | sRGB |
|---|---:|---:|---:|---|---|
| `BC1Unorm` | 8 | 4x4 | 4 | unorm | no |
| `BC1UnormSrgb` | 8 | 4x4 | 4 | unorm | yes |
| `BC3Unorm` | 16 | 4x4 | 4 | unorm | no |
| `BC3UnormSrgb` | 16 | 4x4 | 4 | unorm | yes |
| `BC5Unorm` | 16 | 4x4 | 2 | unorm | no |
| `BC7Unorm` | 16 | 4x4 | 4 | unorm | no |
| `BC7UnormSrgb` | 16 | 4x4 | 4 | unorm | yes |

BC1's fourth channel is the one-bit alpha the format selects between one and
the interpolated value, so it is counted but has no independent storage. BC5
stores two channels only; a renderer that needs three reconstructs the third
from the two, which is the point of the format.

## Derived rules

| Rule | Result |
|---|---|
| `FormatSupportsMultisample` | True for every uncompressed format. A BC block has no per-sample representation. |
| `FormatSupportsRenderTarget` | True for every uncompressed color format, sRGB included. |
| `FormatSupportsStorage` | True for uncompressed, non-sRGB color formats. A storage write is a raw value, not a color to encode. |
| `FormatSupportsBlending` | True for `unorm` and `float` render targets. Integer targets need an integer blend path (Phase 12). |
| `IsFormatViewCompatible(view, base)` | Same layout, same channel count, same component type; only the sRGB flag may differ. Depth-stencil reinterpretation waits for typeless formats in Phase 14. |
| `IsAspectValidForFormat` | `Color` for color formats, `Depth` for every depth format, `Stencil` only when the format has stencil, `DepthStencil` for every depth format. |

## Row alignment

`TextureByteSize(desc, rowAlignment)` pads every row to `rowAlignment`, 256 bytes
by default: D3D12's `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` and a safe upper bound
for Vulkan's optimal copy row pitch on the targets this project supports. A
backend copies with its own pitch and reports its own allocation size in
`TextureInfo::sizeBytes`; the helper exists so resource accounting and the null
backend agree on one arithmetic.
