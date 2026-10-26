#pragma once

#include <cyber/core/types.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/format.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/shader.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

// How often a vertex binding advances.
enum class VertexInputRate : u8
{
    // Once per vertex.
    PerVertex = 0,
    // Once per instance.
    PerInstance,
    // Count of enumerators, for table tests. Not a rate.
    Count
};

// Description of one vertex buffer binding.
struct VertexBinding
{
    // Bytes between consecutive elements. Non-zero and at most 2048.
    u32 strideBytes = 0;
    // Whether the binding advances per vertex or per instance.
    VertexInputRate rate = VertexInputRate::PerVertex;
};

// One shader input, addressed by location. A D3D12 backend maps location N to
// the semantic `TEXCOORDN`, which the Phase 16 HLSL generator also emits.
struct VertexAttribute
{
    // Shader input location. Unique within a layout.
    u32 location = 0;
    // Index into `VertexLayout::bindings`.
    u32 binding = 0;
    // Format of the attribute. Must be a format a vertex fetch can produce.
    Format format = Format::R32Float;
    // Byte offset inside the binding's element. `offset + formatSize` must not
    // exceed the binding's stride.
    u32 offsetBytes = 0;
};

// Vertex input layout: buffers, then attributes that point at them.
struct VertexLayout
{
    memory::Span<const VertexBinding> bindings{};
    memory::Span<const VertexAttribute> attributes{};
};

// True when a format may be fetched as a vertex attribute: uncompressed color
// layout, not sRGB. Float, unorm, and uint attributes are all accepted.
[[nodiscard]] constexpr bool IsVertexFormatSupported(Format format) noexcept
{
    const FormatInfo info = FormatInfoOf(format);
    return info.bytesPerBlock != 0 && !info.compressed && !info.depth && !info.stencil &&
           !info.srgb;
}

// Largest stride a vertex binding may declare. 2048 bytes is D3D12's input-slot
// stride ceiling in practice and far beyond any layout this renderer will use.
inline constexpr u32 kMaxVertexStrideBytes = 2048;

// How a draw's vertices are assembled.
enum class PrimitiveTopology : u8
{
    PointList = 0,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    // Count of enumerators, for table tests. Not a topology.
    Count
};

// Which faces are discarded.
enum class CullMode : u8
{
    None = 0,
    Front,
    Back,
    // Count of enumerators, for table tests. Not a cull mode.
    Count
};

// Which winding is front-facing.
enum class FrontFace : u8
{
    // Clockwise winding is front-facing.
    Clockwise = 0,
    CounterClockwise,
    // Count of enumerators, for table tests. Not a winding.
    Count
};

// How a triangle is filled.
enum class PolygonMode : u8
{
    Fill = 0,
    Line,
    // Count of enumerators, for table tests. Not a mode.
    Count
};

// Rasterizer state. Defaults match a renderer that culls back faces and clips
// depth: cull back, counter-clockwise front, fill, no bias.
struct RasterizerState
{
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::CounterClockwise;
    PolygonMode polygonMode = PolygonMode::Fill;
    // Clamp fragments to the near and far planes instead of discarding them.
    bool depthClipEnabled = true;
    // Added to depth before the test. Zero disables both terms.
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    float depthBiasClamp = 0.0f;
};

// Scalar a blend factor reads.
enum class BlendFactor : u8
{
    Zero = 0,
    One,
    SourceColor,
    OneMinusSourceColor,
    DestinationColor,
    OneMinusDestinationColor,
    SourceAlpha,
    OneMinusSourceAlpha,
    DestinationAlpha,
    OneMinusDestinationAlpha,
    ConstantColor,
    OneMinusConstantColor,
    SourceAlphaSaturate,
    // Count of enumerators, for table tests. Not a factor.
    Count
};

// How source and destination are combined.
enum class BlendOperation : u8
{
    Add = 0,
    Subtract,
    ReverseSubtract,
    Min,
    Max,
    // Count of enumerators, for table tests. Not an operation.
    Count
};

// One blend equation.
struct BlendComponent
{
    BlendFactor source = BlendFactor::One;
    BlendFactor destination = BlendFactor::Zero;
    BlendOperation operation = BlendOperation::Add;
};

// Color write mask bits.
inline constexpr u8 kColorWriteRed = 1U << 0;
inline constexpr u8 kColorWriteGreen = 1U << 1;
inline constexpr u8 kColorWriteBlue = 1U << 2;
inline constexpr u8 kColorWriteAlpha = 1U << 3;
inline constexpr u8 kColorWriteAll =
    kColorWriteRed | kColorWriteGreen | kColorWriteBlue | kColorWriteAlpha;

// Blend and write state for one render target. Blending is opt-in, so a zeroed
// descriptor writes every channel unchanged.
struct BlendState
{
    bool enabled = false;
    BlendComponent color{};
    BlendComponent alpha{};
    // Channels that may be written. Only the low four bits are meaningful.
    u8 writeMask = kColorWriteAll;
};

// One color attachment the pipeline will write.
struct ColorAttachmentState
{
    // Must be a format `FormatSupportsRenderTarget` accepts.
    Format format = Format::RGBA8Unorm;
    BlendState blend{};
};

// Largest number of color attachments a pipeline may declare. Matches the null
// backend's `Limits::maxColorAttachments`.
inline constexpr u32 kMaxColorAttachments = 8;

// Multisample state.
struct MultisampleState
{
    // 1, 2, 4, or 8. Every attachment must have the same count.
    u32 sampleCount = 1;
    // Coverage mask applied to every sample. `0xFFFFFFFF` means all.
    u32 sampleMask = 0xFFFFFFFFU;
    // Convert alpha to coverage instead of blending it.
    bool alphaToCoverage = false;
};

// Stencil operation applied at one point of the stencil test.
enum class StencilOperation : u8
{
    Keep = 0,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap,
    // Count of enumerators, for table tests. Not an operation.
    Count
};

// Stencil state for one facing.
struct StencilFaceState
{
    CompareFunction compare = CompareFunction::Always;
    StencilOperation failOp = StencilOperation::Keep;
    StencilOperation depthFailOp = StencilOperation::Keep;
    StencilOperation passOp = StencilOperation::Keep;
    u8 readMask = 0xFF;
    u8 writeMask = 0xFF;
};

// Depth and stencil state. Depth testing and writing are on by default with the
// comparison reverse-Z wants: a smaller depth value is nearer.
struct DepthStencilState
{
    bool depthTestEnabled = true;
    bool depthWriteEnabled = true;
    CompareFunction depthCompare = CompareFunction::Less;
    bool stencilEnabled = false;
    StencilFaceState front{};
    StencilFaceState back{};
};

// Description of a pipeline cache. Seeding it with the bytes a previous run
// saved lets a backend reuse compiled state; bytes a backend does not
// understand must be ignored rather than rejected, which is what D3D12's
// `ID3D12PipelineLibrary` and Vulkan's `VkPipelineCache` both specify.
struct PipelineCacheDesc
{
    // Previously saved bytes, or an empty span for a new cache.
    memory::Span<const memory::Byte> initialData{};
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// Description of a graphics pipeline.
//
// Every field is plain data so the description can be hashed, compared, and
// serialized. A zeroed descriptor is invalid: it names no shaders and has no
// color attachment.
struct GraphicsPipelineDesc
{
    // Binding layout. Must be a live handle.
    PipelineLayoutHandle layout;
    // Vertex stage. Must be a live vertex shader.
    ShaderHandle vertexShader;
    // Pixel stage. Must be a live pixel shader.
    ShaderHandle pixelShader;
    // Vertex buffers and attributes. A graphics pipeline needs at least one of
    // each.
    VertexLayout vertexLayout{};
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    RasterizerState rasterizer{};
    MultisampleState multisample{};
    DepthStencilState depthStencil{};
    // Format of the depth attachment, or `Format::Count` for no depth
    // attachment. Must be a depth format otherwise.
    Format depthStencilFormat = Format::Count;
    // Color attachments in slot order. Between one and `kMaxColorAttachments`.
    memory::Span<const ColorAttachmentState> colorAttachments{};
    // Optional pipeline cache. An invalid handle means no cache.
    PipelineCacheHandle cache;
    // Optional name for logs and debuggers. Borrowed, and deliberately not part
    // of the description's hash.
    const char* label = nullptr;
};

// Description of a compute pipeline.
struct ComputePipelineDesc
{
    // Binding layout. Must be a live handle.
    PipelineLayoutHandle layout;
    // Compute stage. Must be a live compute shader.
    ShaderHandle computeShader;
    // Optional pipeline cache.
    PipelineCacheHandle cache;
    // Optional name for logs and debuggers. Borrowed, and not hashed.
    const char* label = nullptr;
};

// Which state a pipeline fixes.
enum class PipelineKind : u8
{
    Graphics = 0,
    Compute,
    // Count of enumerators, for table tests. Not a kind.
    Count
};

// Human-readable kind, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* PipelineKindName(PipelineKind kind) noexcept
{
    switch (kind)
    {
    case PipelineKind::Graphics:
        return "graphics";
    case PipelineKind::Compute:
        return "compute";
    case PipelineKind::Count:
        break;
    }
    return "unknown";
}

// What a created pipeline reports about itself. `valid` is false for a stale
// handle, in which case every other field is zero.
struct PipelineInfo
{
    bool valid = false;
    PipelineKind kind = PipelineKind::Graphics;
    // Stable hash of the description the pipeline was created from, or 0.
    u64 hash = 0;
};

// Stable 64-bit hash of a graphics pipeline description. Two descriptions that
// compile to the same object hash equally; a label does not affect the result.
// The hash is deterministic across platforms and builds (ADR 0015).
[[nodiscard]] u64 HashGraphicsPipelineDesc(const GraphicsPipelineDesc& desc) noexcept;

// Stable 64-bit hash of a compute pipeline description.
[[nodiscard]] u64 HashComputePipelineDesc(const ComputePipelineDesc& desc) noexcept;

}  // namespace cyber::rhi
