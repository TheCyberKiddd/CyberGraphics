#include "d3d12_internal.h"

#if defined(CYBER_RHI_HAS_D3D12)

#include <cyber/rhi/format.h>
#include <cyber/rhi/pipeline.h>

#include <cmath>
#include <cstring>

namespace cyber::rhi::detail
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;
using cyber::core::usize;
using cyber::memory::kInvalidHandleIndex;

[[nodiscard]] Status<Error> StatusError(ErrorCode code, const char* message) noexcept
{
    return Status<Error>::Err(Error{code, message});
}

[[nodiscard]] u32 NextGeneration(u32 current) noexcept
{
    const u32 next = current + 1;
    return next == 0 ? 1 : next;
}

// ---------------------------------------------------------------------------
// State conversion
//
// Every converter below is total. A descriptor is validated before it reaches
// this layer, so an enumerator that is not one of the type's values can only
// come from a description that failed validation; mapping it to the API's
// conservative answer keeps a broken descriptor from becoming a broken object
// without a second validation pass.
// ---------------------------------------------------------------------------

[[nodiscard]] D3D12_FILL_MODE ToFillMode(PolygonMode mode) noexcept
{
    return mode == PolygonMode::Line ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
}

[[nodiscard]] D3D12_CULL_MODE ToCullMode(CullMode mode) noexcept
{
    switch (mode)
    {
    case CullMode::None:
        return D3D12_CULL_MODE_NONE;
    case CullMode::Front:
        return D3D12_CULL_MODE_FRONT;
    case CullMode::Back:
        return D3D12_CULL_MODE_BACK;
    case CullMode::Count:
        break;
    }
    return D3D12_CULL_MODE_BACK;
}

[[nodiscard]] D3D12_BLEND ToBlendFactor(BlendFactor factor) noexcept
{
    switch (factor)
    {
    case BlendFactor::Zero:
        return D3D12_BLEND_ZERO;
    case BlendFactor::One:
        return D3D12_BLEND_ONE;
    case BlendFactor::SourceColor:
        return D3D12_BLEND_SRC_COLOR;
    case BlendFactor::OneMinusSourceColor:
        return D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::DestinationColor:
        return D3D12_BLEND_DEST_COLOR;
    case BlendFactor::OneMinusDestinationColor:
        return D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::SourceAlpha:
        return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:
        return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestinationAlpha:
        return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::OneMinusDestinationAlpha:
        return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::ConstantColor:
        return D3D12_BLEND_BLEND_FACTOR;
    case BlendFactor::OneMinusConstantColor:
        return D3D12_BLEND_INV_BLEND_FACTOR;
    case BlendFactor::SourceAlphaSaturate:
        return D3D12_BLEND_SRC_ALPHA_SAT;
    case BlendFactor::Count:
        break;
    }
    return D3D12_BLEND_ONE;
}

[[nodiscard]] D3D12_BLEND_OP ToBlendOperation(BlendOperation operation) noexcept
{
    switch (operation)
    {
    case BlendOperation::Add:
        return D3D12_BLEND_OP_ADD;
    case BlendOperation::Subtract:
        return D3D12_BLEND_OP_SUBTRACT;
    case BlendOperation::ReverseSubtract:
        return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOperation::Min:
        return D3D12_BLEND_OP_MIN;
    case BlendOperation::Max:
        return D3D12_BLEND_OP_MAX;
    case BlendOperation::Count:
        break;
    }
    return D3D12_BLEND_OP_ADD;
}

[[nodiscard]] D3D12_STENCIL_OP ToStencilOperation(StencilOperation operation) noexcept
{
    switch (operation)
    {
    case StencilOperation::Keep:
        return D3D12_STENCIL_OP_KEEP;
    case StencilOperation::Zero:
        return D3D12_STENCIL_OP_ZERO;
    case StencilOperation::Replace:
        return D3D12_STENCIL_OP_REPLACE;
    case StencilOperation::IncrementClamp:
        return D3D12_STENCIL_OP_INCR_SAT;
    case StencilOperation::DecrementClamp:
        return D3D12_STENCIL_OP_DECR_SAT;
    case StencilOperation::Invert:
        return D3D12_STENCIL_OP_INVERT;
    case StencilOperation::IncrementWrap:
        return D3D12_STENCIL_OP_INCR;
    case StencilOperation::DecrementWrap:
        return D3D12_STENCIL_OP_DECR;
    case StencilOperation::Count:
        break;
    }
    return D3D12_STENCIL_OP_KEEP;
}

[[nodiscard]] D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopologyType(PrimitiveTopology topology) noexcept
{
    switch (topology)
    {
    case PrimitiveTopology::PointList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case PrimitiveTopology::Count:
        break;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

[[nodiscard]] D3D12_INPUT_CLASSIFICATION ToInputClassification(VertexInputRate rate) noexcept
{
    return rate == VertexInputRate::PerInstance ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                                                : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
}

// Depth bias arrives as three floats because Vulkan takes floats, and Direct3D 12
// takes an integer constant term next to two float terms. The constant term is
// rounded here, which is the same quantization Vulkan's implementation-defined
// fixed-point conversion performs; a caller that needs the integer exactly
// passes a whole number.
[[nodiscard]] INT ToDepthBias(float value) noexcept
{
    if (!std::isfinite(value))
    {
        return 0;
    }
    if (value >= 16777215.0f)
    {
        return 16777215;
    }
    if (value <= -16777215.0f)
    {
        return -16777215;
    }
    return static_cast<INT>(std::lround(value));
}

[[nodiscard]] D3D12_BLEND_DESC ToBlendDesc(const ColorAttachmentState* attachments,
                                           u32 attachmentCount, bool alphaToCoverage) noexcept
{
    D3D12_BLEND_DESC desc{};
    desc.AlphaToCoverageEnable = alphaToCoverage ? TRUE : FALSE;
    // Blending on a single target cannot be independent, and D3D12 rejects
    // `IndependentBlendEnable` combined with more than one render target on some
    // drivers, so per-target state is always carried but the flag stays off.
    desc.IndependentBlendEnable = FALSE;
    for (u32 index = 0; index < attachmentCount && index < kMaxColorAttachments; ++index)
    {
        const BlendState& blend = attachments[index].blend;
        D3D12_RENDER_TARGET_BLEND_DESC& target = desc.RenderTarget[index];
        target.BlendEnable = blend.enabled ? TRUE : FALSE;
        target.LogicOpEnable = FALSE;
        target.LogicOp = D3D12_LOGIC_OP_NOOP;
        target.SrcBlend = ToBlendFactor(blend.color.source);
        target.DestBlend = ToBlendFactor(blend.color.destination);
        target.BlendOp = ToBlendOperation(blend.color.operation);
        target.SrcBlendAlpha = ToBlendFactor(blend.alpha.source);
        target.DestBlendAlpha = ToBlendFactor(blend.alpha.destination);
        target.BlendOpAlpha = ToBlendOperation(blend.alpha.operation);
        // The RHI's write-mask bits are the API's bits, in the same order.
        target.RenderTargetWriteMask = static_cast<UINT8>(blend.writeMask & kColorWriteAll);
    }
    return desc;
}

[[nodiscard]] D3D12_RASTERIZER_DESC ToRasterizerDesc(const RasterizerState& state) noexcept
{
    D3D12_RASTERIZER_DESC desc{};
    desc.FillMode = ToFillMode(state.polygonMode);
    desc.CullMode = ToCullMode(state.cullMode);
    desc.FrontCounterClockwise = state.frontFace == FrontFace::CounterClockwise ? TRUE : FALSE;
    desc.DepthBias = ToDepthBias(state.depthBiasConstant);
    desc.DepthBiasClamp = state.depthBiasClamp;
    desc.SlopeScaledDepthBias = state.depthBiasSlope;
    desc.DepthClipEnable = state.depthClipEnabled ? TRUE : FALSE;
    desc.MultisampleEnable = FALSE;
    desc.AntialiasedLineEnable = FALSE;
    desc.ForcedSampleCount = 0;
    desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    return desc;
}

[[nodiscard]] D3D12_DEPTH_STENCILOP_DESC ToStencilFace(const StencilFaceState& face) noexcept
{
    D3D12_DEPTH_STENCILOP_DESC desc{};
    desc.StencilFailOp = ToStencilOperation(face.failOp);
    desc.StencilDepthFailOp = ToStencilOperation(face.depthFailOp);
    desc.StencilPassOp = ToStencilOperation(face.passOp);
    desc.StencilFunc = ToComparisonFunc(face.compare);
    return desc;
}

[[nodiscard]] D3D12_DEPTH_STENCIL_DESC ToDepthStencilDesc(const DepthStencilState& state) noexcept
{
    D3D12_DEPTH_STENCIL_DESC desc{};
    // The API has one depth switch: disabling the test disables writes with it.
    // The RHI keeps the two separate because Vulkan does, so a description that
    // tests off and writes on means "no depth" here, which is the only reading
    // the API can express.
    desc.DepthEnable = state.depthTestEnabled ? TRUE : FALSE;
    desc.DepthWriteMask =
        state.depthWriteEnabled ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    desc.DepthFunc = ToComparisonFunc(state.depthCompare);
    desc.StencilEnable = state.stencilEnabled ? TRUE : FALSE;
    desc.StencilReadMask = state.front.readMask;
    desc.StencilWriteMask = state.front.writeMask;
    desc.FrontFace = ToStencilFace(state.front);
    desc.BackFace = ToStencilFace(state.back);
    return desc;
}

[[nodiscard]] D3D12_SHADER_BYTECODE ToBytecode(const D3D12ShaderSlot& shader) noexcept
{
    D3D12_SHADER_BYTECODE bytecode{};
    bytecode.pShaderBytecode = shader.code;
    bytecode.BytecodeLength = static_cast<SIZE_T>(shader.codeSize);
    return bytecode;
}

// The format of one vertex-fetch element.
//
// This is not `ToDxgiFormat`, because a vertex attribute and a texel are not the
// same question. A three-component float is the clearest case: the API has
// `DXGI_FORMAT_R32G32B32_FLOAT` and the input assembler accepts it, while the
// texel paths (render target, storage, typed view) refuse it, which is why the
// texture conversion answers `DXGI_FORMAT_UNKNOWN` for it.
[[nodiscard]] DXGI_FORMAT ToVertexFormat(Format format) noexcept
{
    if (format == Format::R32G32B32Float)
    {
        return DXGI_FORMAT_R32G32B32_FLOAT;
    }
    return ToDxgiFormat(format);
}

// Builds the input layout's element array.
//
// A shader input is addressed by location, and Direct3D 12 matches an input by
// semantic name and index. `TEXCOORD` with the location as the index is the
// mapping the RHI's contract names, so the same vertex layout drives both
// backends without the caller knowing which semantic a shader uses.
[[nodiscard]] u32 FillInputElements(const VertexLayout& layout, D3D12_INPUT_ELEMENT_DESC* elements,
                                    u32 capacity) noexcept
{
    u32 written = 0;
    for (usize index = 0; index < layout.attributes.Size() && written < capacity; ++index)
    {
        const VertexAttribute& attribute = layout.attributes[index];
        D3D12_INPUT_ELEMENT_DESC& element = elements[written];
        element.SemanticName = "TEXCOORD";
        element.SemanticIndex = attribute.location;
        element.Format = ToVertexFormat(attribute.format);
        element.InputSlot = attribute.binding;
        element.AlignedByteOffset = attribute.offsetBytes;
        element.InputSlotClass = ToInputClassification(layout.bindings[attribute.binding].rate);
        element.InstanceDataStepRate =
            layout.bindings[attribute.binding].rate == VertexInputRate::PerInstance ? 1 : 0;
        ++written;
    }
    return written;
}

// ---------------------------------------------------------------------------
// Validation
//
// This mirrors the null backend's rules, which are the contract's rules. A
// description the reference backend accepts must be accepted here too, or the
// two backends disagree about the API and one of them is wrong.
// ---------------------------------------------------------------------------

[[nodiscard]] Status<Error> ValidateBlend(const ColorAttachmentState& attachment) noexcept
{
    if (attachment.blend.writeMask > kColorWriteAll)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: blend write mask is invalid");
    }
    if (!attachment.blend.enabled)
    {
        return Status<Error>::Ok();
    }
    if (!FormatSupportsBlending(attachment.format))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: blending is not supported by the attachment format");
    }
    const BlendComponent* components[2] = {&attachment.blend.color, &attachment.blend.alpha};
    for (const BlendComponent* component : components)
    {
        if (component->source >= BlendFactor::Count ||
            component->destination >= BlendFactor::Count ||
            component->operation >= BlendOperation::Count)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: blend factor or operation is not an enumerator");
        }
    }
    return Status<Error>::Ok();
}

[[nodiscard]] Status<Error> ValidateGraphicsDesc(const D3D12Device& device,
                                                 const GraphicsPipelineDesc& desc,
                                                 const D3D12ShaderSlot& vertex,
                                                 const D3D12ShaderSlot& pixel) noexcept
{
    if (vertex.stage != ShaderStage::Vertex)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: graphics pipeline vertex shader is not a vertex stage");
    }
    if (pixel.stage != ShaderStage::Pixel)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: graphics pipeline pixel shader is not a pixel stage");
    }

    const VertexLayout& layout = desc.vertexLayout;
    if (layout.bindings.Data() == nullptr || layout.bindings.Empty())
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: graphics pipeline has no vertex binding");
    }
    if (layout.attributes.Data() == nullptr || layout.attributes.Empty())
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: graphics pipeline has no vertex attribute");
    }
    if (layout.bindings.Size() > device.capabilities.limits.maxVertexBuffers)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: more vertex bindings than the device allows");
    }
    if (layout.attributes.Size() > device.capabilities.limits.maxVertexAttributes)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: more vertex attributes than the device allows");
    }

    for (usize index = 0; index < layout.bindings.Size(); ++index)
    {
        const VertexBinding& binding = layout.bindings[index];
        if (binding.strideBytes == 0 || binding.strideBytes > kMaxVertexStrideBytes)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: vertex binding stride is zero or above 2048 bytes");
        }
        if (binding.rate >= VertexInputRate::Count)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: vertex input rate is not an input rate");
        }
    }

    for (usize index = 0; index < layout.attributes.Size(); ++index)
    {
        const VertexAttribute& attribute = layout.attributes[index];
        if (!IsVertexFormatSupported(attribute.format))
        {
            return StatusError(
                ErrorCode::InvalidArgument,
                "d3d12 backend: vertex attribute format cannot be fetched as vertex input");
        }
        if (attribute.binding >= layout.bindings.Size())
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: vertex attribute names no such binding");
        }
        const u64 end =
            static_cast<u64>(attribute.offsetBytes) + FormatBytesPerBlock(attribute.format);
        if (end > layout.bindings[attribute.binding].strideBytes)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: vertex attribute extends past its binding stride");
        }
        for (usize other = 0; other < index; ++other)
        {
            if (layout.attributes[other].location == attribute.location)
            {
                return StatusError(ErrorCode::InvalidArgument,
                                   "d3d12 backend: vertex layout repeats an attribute location");
            }
        }
    }

    if (desc.topology >= PrimitiveTopology::Count || desc.rasterizer.cullMode >= CullMode::Count ||
        desc.rasterizer.frontFace >= FrontFace::Count ||
        desc.rasterizer.polygonMode >= PolygonMode::Count)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: rasterizer or topology state is invalid");
    }

    if (desc.multisample.sampleCount != 1 && desc.multisample.sampleCount != 2 &&
        desc.multisample.sampleCount != 4 && desc.multisample.sampleCount != 8)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline sample count must be 1, 2, 4, or 8");
    }
    if (desc.multisample.alphaToCoverage && desc.multisample.sampleCount == 1)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: alpha to coverage needs more than one sample");
    }

    if (desc.depthStencil.depthCompare >= CompareFunction::Count)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: depth comparison is not a comparison");
    }
    if (desc.depthStencil.stencilEnabled &&
        (desc.depthStencil.front.compare >= CompareFunction::Count ||
         desc.depthStencil.back.compare >= CompareFunction::Count ||
         desc.depthStencil.front.failOp >= StencilOperation::Count ||
         desc.depthStencil.front.depthFailOp >= StencilOperation::Count ||
         desc.depthStencil.front.passOp >= StencilOperation::Count ||
         desc.depthStencil.back.failOp >= StencilOperation::Count ||
         desc.depthStencil.back.depthFailOp >= StencilOperation::Count ||
         desc.depthStencil.back.passOp >= StencilOperation::Count))
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: stencil state has an invalid enumerator");
    }

    const bool needsDepth = desc.depthStencil.depthTestEnabled ||
                            desc.depthStencil.depthWriteEnabled || desc.depthStencil.stencilEnabled;
    if (needsDepth && desc.depthStencilFormat == Format::Count)
    {
        return StatusError(
            ErrorCode::InvalidArgument,
            "d3d12 backend: pipeline has depth state but no depth attachment format");
    }
    if (desc.depthStencilFormat != Format::Count)
    {
        if (!IsDepthFormat(desc.depthStencilFormat))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: depth attachment format is not a depth format");
        }
        if (desc.depthStencil.stencilEnabled && !IsStencilFormat(desc.depthStencilFormat))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: stencil state needs a format with stencil");
        }
        if (desc.multisample.sampleCount > 1 && !FormatSupportsMultisample(desc.depthStencilFormat))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: depth format cannot be multisampled");
        }
    }

    if (desc.colorAttachments.Data() == nullptr || desc.colorAttachments.Empty())
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: graphics pipeline has no color attachment");
    }
    if (desc.colorAttachments.Size() > kMaxColorAttachments ||
        desc.colorAttachments.Size() > device.capabilities.limits.maxColorAttachments)
    {
        return StatusError(ErrorCode::OutOfRange,
                           "d3d12 backend: graphics pipeline has too many color attachments");
    }

    for (usize index = 0; index < desc.colorAttachments.Size(); ++index)
    {
        const ColorAttachmentState& attachment = desc.colorAttachments[index];
        if (!FormatSupportsRenderTarget(attachment.format))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: color attachment format cannot be a render target");
        }
        if (desc.multisample.sampleCount > 1 && !FormatSupportsMultisample(attachment.format))
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: color attachment format cannot be multisampled");
        }
        const Status<Error> blend = ValidateBlend(attachment);
        if (!blend)
        {
            return blend;
        }
    }

    return Status<Error>::Ok();
}

// A pipeline library entry is addressed by the description that produced it, so
// the caller does not need to invent a key. Names are only used for the store,
// which requires one; the hash of the description is already unique per
// compiled object and is what goes in.
inline constexpr usize kPipelineNameChars = 24;

void WritePipelineName(u64 hash, wchar_t (&out)[kPipelineNameChars]) noexcept
{
    static constexpr wchar_t kDigits[] = L"0123456789abcdef";
    out[0] = L'p';
    for (usize index = 0; index < 16; ++index)
    {
        const u32 shift = static_cast<u32>(60 - index * 4);
        out[index + 1] = kDigits[(hash >> shift) & 0xFULL];
    }
    out[17] = L'\0';
}

// Turns a failed pipeline-state creation into an error that says what happened.
// Out of memory is the device's problem; everything else is a description the
// API refused, which in practice means the shaders and the pipeline state
// disagree.
[[nodiscard]] Status<Error> StateCreationFailure(HRESULT result, const char* what) noexcept
{
    if (result == E_OUTOFMEMORY)
    {
        return StatusError(ErrorCode::Internal, what);
    }
    return StatusError(ErrorCode::InvalidArgument, what);
}

// Creates the state, reusing a cached object when the description is already in
// the library. A cache that cannot answer is a miss, never a failure: the
// library reports an unknown name with `E_INVALIDARG`, which is why the lookup's
// result is tested rather than inspected.
template <typename CreateState, typename Lookup>
[[nodiscard]] Status<Error> AcquireState(ID3D12PipelineLibrary* library, const wchar_t* name,
                                         CreateState&& create, Lookup&& lookup,
                                         ID3D12PipelineState** out) noexcept
{
    *out = nullptr;
    bool cached = false;
    if (library != nullptr)
    {
        cached = SUCCEEDED(lookup(library, name, out)) && *out != nullptr;
    }

    if (!cached)
    {
        const HRESULT created = create(out);
        if (FAILED(created) || *out == nullptr)
        {
            *out = nullptr;
            return StateCreationFailure(
                created, "d3d12 backend: pipeline state was refused; check that the shaders "
                         "match the pipeline description");
        }
        if (library != nullptr)
        {
            // Storing cannot fail the creation: a cache that cannot grow is slower,
            // not wrong.
            (void)library->StorePipeline(name, *out);
        }
    }
    return Status<Error>::Ok();
}

// Writes a freshly created state into the device table, releasing it if the
// table cannot grow.
[[nodiscard]] Status<Error> StorePipeline(D3D12Device& device, ID3D12PipelineState* state,
                                          PipelineKind kind, u64 hash,
                                          const GraphicsPipelineDesc* graphics,
                                          PipelineLayoutHandle layout, PipelineHandle* out) noexcept
{
    u32 index = kInvalidHandleIndex;
    if (device.freePipelineSlots.Size() > 0)
    {
        index = device.freePipelineSlots[device.freePipelineSlots.Size() - 1];
        (void)device.freePipelineSlots.TryResize(device.freePipelineSlots.Size() - 1);
    }
    else
    {
        if (!device.pipelines.TryPushBack(D3D12PipelineSlot{}))
        {
            state->Release();
            return StatusError(ErrorCode::Internal, "d3d12 backend: pipeline table growth failed");
        }
        index = static_cast<u32>(device.pipelines.Size() - 1);
    }

    D3D12PipelineSlot& slot = device.pipelines[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.state = state;
    slot.kind = kind;
    slot.hash = hash;
    slot.layout = layout;
    slot.vertexBindingCount = 0;
    slot.sampleCount = 1;
    slot.colorAttachmentCount = 0;
    slot.depthStencilFormat = Format::Count;
    if (graphics != nullptr)
    {
        const VertexLayout& vertexLayout = graphics->vertexLayout;
        for (usize binding = 0;
             binding < vertexLayout.bindings.Size() && binding < kMaxVertexBindings; ++binding)
        {
            slot.vertexStrides[binding] = vertexLayout.bindings[binding].strideBytes;
        }
        slot.vertexBindingCount = static_cast<u32>(vertexLayout.bindings.Size());
        slot.sampleCount = graphics->multisample.sampleCount;
        slot.colorAttachmentCount = graphics->colorAttachments.Size() > kMaxColorAttachments
                                        ? kMaxColorAttachments
                                        : static_cast<u32>(graphics->colorAttachments.Size());
        for (u32 attachment = 0; attachment < slot.colorAttachmentCount; ++attachment)
        {
            slot.colorFormats[attachment] = graphics->colorAttachments[attachment].format;
        }
        slot.depthStencilFormat = graphics->depthStencilFormat;
    }

    PipelineHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

}  // namespace

// ---------------------------------------------------------------------------
// Slot resolution
// ---------------------------------------------------------------------------

D3D12PipelineSlot* Resolve(D3D12Device& device, PipelineHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.pipelines.Size())
    {
        return nullptr;
    }
    D3D12PipelineSlot& slot = device.pipelines[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

D3D12PipelineCacheSlot* Resolve(D3D12Device& device, PipelineCacheHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= device.pipelineCaches.Size())
    {
        return nullptr;
    }
    D3D12PipelineCacheSlot& slot = device.pipelineCaches[handle.index];
    return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
}

// ---------------------------------------------------------------------------
// Graphics pipelines
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateGraphicsPipeline(void* internal, const GraphicsPipelineDesc& desc,
                                          PipelineHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline creation needs a device and an output");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    const D3D12PipelineLayoutSlot* pipelineLayout = Resolve(device, desc.layout);
    if (pipelineLayout == nullptr || pipelineLayout->rootSignature == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: graphics pipeline names a stale pipeline layout");
    }
    const D3D12ShaderSlot* vertex = Resolve(device, desc.vertexShader);
    if (vertex == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: graphics pipeline names a stale vertex shader");
    }
    const D3D12ShaderSlot* pixel = Resolve(device, desc.pixelShader);
    if (pixel == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: graphics pipeline names a stale pixel shader");
    }

    D3D12PipelineCacheSlot* cache = nullptr;
    if (desc.cache.IsValid())
    {
        cache = Resolve(device, desc.cache);
        if (cache == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: graphics pipeline names a stale cache");
        }
    }

    const Status<Error> validated = ValidateGraphicsDesc(device, desc, *vertex, *pixel);
    if (!validated)
    {
        return validated;
    }

    D3D12_INPUT_ELEMENT_DESC elements[32]{};
    const u32 elementCount = FillInputElements(desc.vertexLayout, elements, static_cast<u32>(32));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.pRootSignature = pipelineLayout->rootSignature;
    state.VS = ToBytecode(*vertex);
    state.PS = ToBytecode(*pixel);
    state.BlendState =
        ToBlendDesc(desc.colorAttachments.Data(), static_cast<u32>(desc.colorAttachments.Size()),
                    desc.multisample.alphaToCoverage);
    state.SampleMask = desc.multisample.sampleMask;
    state.RasterizerState = ToRasterizerDesc(desc.rasterizer);
    state.DepthStencilState = ToDepthStencilDesc(desc.depthStencil);
    state.InputLayout = {elementCount > 0 ? elements : nullptr, elementCount};
    state.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
    state.PrimitiveTopologyType = ToTopologyType(desc.topology);
    state.NumRenderTargets = static_cast<UINT>(desc.colorAttachments.Size());
    for (u32 index = 0; index < kMaxColorAttachments; ++index)
    {
        state.RTVFormats[index] = index < desc.colorAttachments.Size()
                                      ? ToDxgiFormat(desc.colorAttachments[index].format)
                                      : DXGI_FORMAT_UNKNOWN;
    }
    state.DSVFormat = desc.depthStencilFormat == Format::Count
                          ? DXGI_FORMAT_UNKNOWN
                          : ToDxgiFormat(desc.depthStencilFormat);
    state.SampleDesc.Count = desc.multisample.sampleCount;
    // Direct3D 12 leaves the quality level to the driver, which is what 0 asks
    // for; the RHI has no per-device quality selection to forward.
    state.SampleDesc.Quality = 0;
    state.NodeMask = 0;
    state.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
    state.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    const u64 hash = HashGraphicsPipelineDesc(desc);
    wchar_t name[kPipelineNameChars]{};
    WritePipelineName(hash, name);

    ID3D12PipelineState* pipelineState = nullptr;
    const Status<Error> acquired = AcquireState(
        cache != nullptr ? cache->library : nullptr, name,
        [&device, &state](ID3D12PipelineState** result) noexcept
        {
            return device.device->CreateGraphicsPipelineState(&state, __uuidof(ID3D12PipelineState),
                                                              reinterpret_cast<void**>(result));
        },
        [&state](ID3D12PipelineLibrary* library, const wchar_t* entryName,
                 ID3D12PipelineState** result) noexcept
        {
            return library->LoadGraphicsPipeline(entryName, &state, __uuidof(ID3D12PipelineState),
                                                 reinterpret_cast<void**>(result));
        },
        &pipelineState);
    if (!acquired)
    {
        return acquired;
    }
    NameObject(pipelineState, desc.label);

    return StorePipeline(device, pipelineState, PipelineKind::Graphics, hash, &desc, desc.layout,
                         out);
}

Status<Error> D3D12DestroyGraphicsPipeline(void* internal, PipelineHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12PipelineSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline handle is stale or invalid");
    }
    if (slot->kind != PipelineKind::Graphics)
    {
        // The two destroy entries are separate in the RHI so a mistaken call is
        // caught rather than silently accepted.
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline is not a graphics pipeline");
    }
    if (slot->state != nullptr)
    {
        slot->state->Release();
        slot->state = nullptr;
    }
    slot->alive = false;
    slot->hash = 0;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freePipelineSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: pipeline free list growth failed");
    }
    return Status<Error>::Ok();
}

// ---------------------------------------------------------------------------
// Compute pipelines
// ---------------------------------------------------------------------------

Status<Error> D3D12CreateComputePipeline(void* internal, const ComputePipelineDesc& desc,
                                         PipelineHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline creation needs a device and an output");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    if (!device.capabilities.features.Has(Feature::Compute))
    {
        return StatusError(ErrorCode::Unsupported, "d3d12 backend: compute is not supported");
    }

    const D3D12PipelineLayoutSlot* pipelineLayout = Resolve(device, desc.layout);
    if (pipelineLayout == nullptr || pipelineLayout->rootSignature == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: compute pipeline names a stale pipeline layout");
    }
    const D3D12ShaderSlot* shader = Resolve(device, desc.computeShader);
    if (shader == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: compute pipeline names a stale compute shader");
    }
    if (shader->stage != ShaderStage::Compute)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: compute pipeline shader is not a compute stage");
    }

    D3D12PipelineCacheSlot* cache = nullptr;
    if (desc.cache.IsValid())
    {
        cache = Resolve(device, desc.cache);
        if (cache == nullptr)
        {
            return StatusError(ErrorCode::InvalidArgument,
                               "d3d12 backend: compute pipeline names a stale cache");
        }
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC state{};
    state.pRootSignature = pipelineLayout->rootSignature;
    state.CS = ToBytecode(*shader);
    state.NodeMask = 0;
    state.CachedPSO = D3D12_CACHED_PIPELINE_STATE{};
    state.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    const u64 hash = HashComputePipelineDesc(desc);
    wchar_t name[kPipelineNameChars]{};
    WritePipelineName(hash, name);

    ID3D12PipelineState* pipelineState = nullptr;
    const Status<Error> acquired = AcquireState(
        cache != nullptr ? cache->library : nullptr, name,
        [&device, &state](ID3D12PipelineState** result) noexcept
        {
            return device.device->CreateComputePipelineState(&state, __uuidof(ID3D12PipelineState),
                                                             reinterpret_cast<void**>(result));
        },
        [&state](ID3D12PipelineLibrary* library, const wchar_t* entryName,
                 ID3D12PipelineState** result) noexcept
        {
            return library->LoadComputePipeline(entryName, &state, __uuidof(ID3D12PipelineState),
                                                reinterpret_cast<void**>(result));
        },
        &pipelineState);
    if (!acquired)
    {
        return acquired;
    }
    NameObject(pipelineState, desc.label);

    return StorePipeline(device, pipelineState, PipelineKind::Compute, hash, nullptr, desc.layout,
                         out);
}

Status<Error> D3D12DestroyComputePipeline(void* internal, PipelineHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12PipelineSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline handle is stale or invalid");
    }
    if (slot->kind != PipelineKind::Compute)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: pipeline is not a compute pipeline");
    }
    if (slot->state != nullptr)
    {
        slot->state->Release();
        slot->state = nullptr;
    }
    slot->alive = false;
    slot->hash = 0;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freePipelineSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: pipeline free list growth failed");
    }
    return Status<Error>::Ok();
}

PipelineInfo D3D12PipelineInfo(const void* internal, PipelineHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return PipelineInfo{};
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    if (!handle.IsValid() || handle.index >= device.pipelines.Size())
    {
        return PipelineInfo{};
    }
    const D3D12PipelineSlot& slot = device.pipelines[handle.index];
    if (!slot.alive || slot.generation != handle.generation || slot.state == nullptr)
    {
        return PipelineInfo{};
    }
    PipelineInfo info{};
    info.valid = true;
    info.kind = slot.kind;
    info.hash = slot.hash;
    return info;
}

// ---------------------------------------------------------------------------
// Pipeline caches
// ---------------------------------------------------------------------------

Status<Error> D3D12CreatePipelineCache(void* internal, const PipelineCacheDesc& desc,
                                       PipelineCacheHandle* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: cache creation needs a device and an output");
    }
    if (desc.initialData.Data() == nullptr && !desc.initialData.Empty())
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: cache seed bytes are null");
    }

    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);

    // `ID3D12PipelineLibrary` is the API's own persistence mechanism: it stores
    // compiled state and serializes it, and the contract's rule that bytes a
    // backend does not understand are ignored rather than rejected is exactly
    // what it does when a blob comes from another driver or compiler version.
    ComOwner<ID3D12Device1> device1;
    ID3D12PipelineLibrary* library = nullptr;
    if (SUCCEEDED(device.device->QueryInterface(__uuidof(ID3D12Device1), device1.PutVoid())) &&
        device1.IsValid())
    {
        const void* blob = desc.initialData.Empty() ? nullptr : desc.initialData.Data();
        const SIZE_T size = static_cast<SIZE_T>(desc.initialData.Size() * sizeof(memory::Byte));
        HRESULT created = device1->CreatePipelineLibrary(
            blob, size, __uuidof(ID3D12PipelineLibrary), reinterpret_cast<void**>(&library));
        if (FAILED(created) || library == nullptr)
        {
            library = nullptr;
            // Seed bytes this device cannot use are not an error: the caller asked
            // for a cache, and a cache that starts empty still does its job.
            created = device1->CreatePipelineLibrary(nullptr, 0, __uuidof(ID3D12PipelineLibrary),
                                                     reinterpret_cast<void**>(&library));
            if (FAILED(created) || library == nullptr)
            {
                library = nullptr;
            }
        }
    }

    u32 index = kInvalidHandleIndex;
    if (device.freePipelineCacheSlots.Size() > 0)
    {
        index = device.freePipelineCacheSlots[device.freePipelineCacheSlots.Size() - 1];
        (void)device.freePipelineCacheSlots.TryResize(device.freePipelineCacheSlots.Size() - 1);
    }
    else
    {
        if (!device.pipelineCaches.TryPushBack(D3D12PipelineCacheSlot{}))
        {
            if (library != nullptr)
            {
                library->Release();
            }
            return StatusError(ErrorCode::Internal, "d3d12 backend: cache table growth failed");
        }
        index = static_cast<u32>(device.pipelineCaches.Size() - 1);
    }

    D3D12PipelineCacheSlot& slot = device.pipelineCaches[index];
    slot.generation = NextGeneration(slot.generation);
    slot.alive = true;
    slot.library = library;

    PipelineCacheHandle handle;
    handle.index = index;
    handle.generation = slot.generation;
    *out = handle;
    return Status<Error>::Ok();
}

Status<Error> D3D12DestroyPipelineCache(void* internal, PipelineCacheHandle handle) noexcept
{
    if (internal == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument, "d3d12 backend: device is null");
    }
    auto& device = *static_cast<D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    D3D12PipelineCacheSlot* slot = Resolve(device, handle);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: cache handle is stale or invalid");
    }
    if (slot->library != nullptr)
    {
        slot->library->Release();
        slot->library = nullptr;
    }
    slot->alive = false;
    slot->generation = NextGeneration(slot->generation);
    if (!device.freePipelineCacheSlots.TryPushBack(handle.index))
    {
        return StatusError(ErrorCode::Internal, "d3d12 backend: cache free list growth failed");
    }
    return Status<Error>::Ok();
}

Status<Error> D3D12PipelineCacheBytes(const void* internal, PipelineCacheHandle cache,
                                      memory::Allocator&, DynamicArray<memory::Byte>* out) noexcept
{
    if (internal == nullptr || out == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: reading cache bytes needs a device and an output");
    }
    const auto& device = *static_cast<const D3D12Device*>(internal);
    const std::lock_guard<std::mutex> lock(device.mutex);
    const D3D12PipelineCacheSlot* slot = Resolve(const_cast<D3D12Device&>(device), cache);
    if (slot == nullptr)
    {
        return StatusError(ErrorCode::InvalidArgument,
                           "d3d12 backend: cache handle is stale or invalid");
    }
    // A cache with no library, or one that has stored nothing yet, has no bytes
    // to save. That is an empty answer, not a failure.
    if (slot->library == nullptr)
    {
        return Status<Error>::Ok();
    }

    // The library knows how many bytes it needs; asking first is how the API is
    // meant to be driven, since `Serialize` cannot size the buffer itself.
    const SIZE_T size = slot->library->GetSerializedSize();
    if (size == 0)
    {
        return Status<Error>::Ok();
    }
    if (!out->TryResize(static_cast<usize>(size / sizeof(memory::Byte))))
    {
        return StatusError(ErrorCode::Internal,
                           "d3d12 backend: cache byte buffer allocation failed");
    }
    const HRESULT serialized = slot->library->Serialize(
        out->Data(), static_cast<SIZE_T>(size / sizeof(memory::Byte) * sizeof(memory::Byte)));
    if (FAILED(serialized))
    {
        (void)out->TryResize(0);
        return StatusError(ErrorCode::Internal, "d3d12 backend: cache serialization failed");
    }
    return Status<Error>::Ok();
}

}  // namespace cyber::rhi::detail

#endif
