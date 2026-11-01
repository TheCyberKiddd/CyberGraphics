// The RHI's own rule set, implemented once and called by the null backend and
// by `validating_device.cpp` (ADR 0018). The rules are the Phase 11 checks the
// null backend has always enforced, kept byte for byte so a rule cannot drift
// between callers; their comments stay with them.

#include "validation.h"

#include <cyber/core/error.h>
#include <cyber/core/types.h>
#include <cyber/rhi/format.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/shader.h>
#include <cyber/rhi/texture.h>

#include <cstring>

namespace cyber::rhi::validation
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::usize;

bool Fail(Error* out, ErrorCode code, const char* message) noexcept
{
    if (out != nullptr)
    {
        out->code = code;
        out->message = message;
    }
    return false;
}

bool ValidateBufferDesc(const Limits& limits, const BufferDesc& desc, Error* out) noexcept
{
    if (desc.usage == BufferUsage::None)
    {
        return Fail(out, ErrorCode::InvalidArgument, "buffer descriptor has no usage");
    }
    if (desc.sizeBytes == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "buffer size is zero");
    }
    if (desc.sizeBytes > limits.maxBufferBytes)
    {
        return Fail(out, ErrorCode::OutOfRange, "buffer size exceeds the device limit");
    }
    const bool hostVisible = HasUsage(desc.usage, BufferUsage::HostVisible);
    const bool readback = HasUsage(desc.usage, BufferUsage::Readback);
    if (hostVisible && readback)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a buffer cannot be both host visible and readback");
    }
    if ((hostVisible || readback) && (desc.usage & kBufferPipelineUsages) != BufferUsage::None)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a host visible or readback buffer cannot be bound to a pipeline");
    }
    if (desc.elementStride != 0 && (desc.elementStride % 4 != 0 || desc.elementStride > 2048))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "buffer element stride must be a multiple of 4 and at most 2048 bytes");
    }
    if (desc.lifetime >= MemoryLifetime::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "buffer lifetime is not a lifetime");
    }
    return true;
}

// Shape rules shared by a texture and a view of it.
bool ValidateTextureShape(const Limits& limits, const TextureDesc& desc, Error* out) noexcept
{
    if (desc.dimension == TextureDimension::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture dimension is not a dimension");
    }
    const FormatInfo format = FormatInfoOf(desc.format);
    if (format.bytesPerBlock == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture format is not a format");
    }
    if (desc.usage == TextureUsage::None)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture descriptor has no usage");
    }
    if (desc.lifetime >= MemoryLifetime::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture lifetime is not a lifetime");
    }
    if (desc.width == 0 || desc.height == 0 || desc.depth == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture extent has a zero dimension");
    }
    if (desc.arrayLayers == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture has zero array layers");
    }
    if (desc.mipLevels == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture has zero mip levels");
    }

    switch (desc.dimension)
    {
    case TextureDimension::Dim1D:
        if (desc.height != 1 || desc.depth != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a 1d texture has height and depth of one");
        }
        break;
    case TextureDimension::Dim2D:
        if (desc.depth != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument, "a 2d texture has a depth of one");
        }
        break;
    case TextureDimension::Dim3D:
        if (desc.arrayLayers != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a 3d texture has exactly one array layer");
        }
        break;
    case TextureDimension::Cube:
        if (desc.depth != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument, "a cube texture has a depth of one");
        }
        if (desc.width != desc.height)
        {
            return Fail(out, ErrorCode::InvalidArgument, "a cube texture must be square");
        }
        if (desc.arrayLayers % 6 != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a cube texture layer count is a multiple of six");
        }
        break;
    case TextureDimension::Count:
        break;
    }

    const u32 maxMips = MaxMipLevels(desc.dimension, desc.width, desc.height, desc.depth);
    if (desc.mipLevels > maxMips)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "texture mip count exceeds the chain its extent allows");
    }

    // Limits are a separate code so a caller that read the capabilities can tell
    // "I asked for too much" from "I described something impossible".
    if (desc.dimension == TextureDimension::Dim1D && desc.width > limits.maxTexture1D)
    {
        return Fail(out, ErrorCode::OutOfRange, "1d texture width exceeds the device limit");
    }
    if (desc.dimension == TextureDimension::Dim2D &&
        (desc.width > limits.maxTexture2D || desc.height > limits.maxTexture2D))
    {
        return Fail(out, ErrorCode::OutOfRange, "2d texture extent exceeds the device limit");
    }
    if (desc.dimension == TextureDimension::Cube &&
        (desc.width > limits.maxTextureCube || desc.height > limits.maxTextureCube))
    {
        return Fail(out, ErrorCode::OutOfRange, "cube texture extent exceeds the device limit");
    }
    if (desc.dimension == TextureDimension::Dim3D &&
        (desc.width > limits.maxTexture3D || desc.height > limits.maxTexture3D ||
         desc.depth > limits.maxTexture3D))
    {
        return Fail(out, ErrorCode::OutOfRange, "3d texture extent exceeds the device limit");
    }
    if (desc.arrayLayers > limits.maxTextureArrayLayers)
    {
        return Fail(out, ErrorCode::OutOfRange, "texture layer count exceeds the device limit");
    }
    return true;
}

bool ValidateTextureDesc(const Capabilities& capabilities, const TextureDesc& desc,
                         Error* out) noexcept
{
    if (!ValidateTextureShape(capabilities.limits, desc, out))
    {
        return false;
    }

    const FormatInfo format = FormatInfoOf(desc.format);
    const bool multisampled = desc.sampleCount > 1;
    if (desc.sampleCount != 1 && desc.sampleCount != 2 && desc.sampleCount != 4 &&
        desc.sampleCount != 8)
    {
        return Fail(out, ErrorCode::InvalidArgument, "texture sample count must be 1, 2, 4, or 8");
    }
    if (multisampled)
    {
        if (desc.mipLevels != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a multisampled texture has exactly one mip level");
        }
        if (desc.dimension == TextureDimension::Dim1D ||
            desc.dimension == TextureDimension::Dim3D || desc.dimension == TextureDimension::Cube)
        {
            return Fail(out, ErrorCode::InvalidArgument, "only a 2d texture may be multisampled");
        }
        if (!FormatSupportsMultisample(desc.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a block compressed format cannot be multisampled");
        }
    }

    const bool renderTarget = HasUsage(desc.usage, TextureUsage::RenderTarget);
    const bool depthStencil = HasUsage(desc.usage, TextureUsage::DepthStencil);
    if (renderTarget && depthStencil)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a texture cannot be both a color and a depth attachment");
    }
    if (renderTarget && !FormatSupportsRenderTarget(desc.format))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "the texture format cannot be a color render target");
    }
    if (depthStencil && !format.depth)
    {
        return Fail(out, ErrorCode::InvalidArgument, "depth-stencil usage requires a depth format");
    }
    // A depth format nobody reads is a descriptor mistake: it can be an
    // attachment, sampled, or a copy target, but it cannot be none of those.
    if (!depthStencil && format.depth && !HasUsage(desc.usage, TextureUsage::Sampled) &&
        !HasUsage(desc.usage, TextureUsage::CopySource) &&
        !HasUsage(desc.usage, TextureUsage::CopyDestination))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a depth texture needs depth-stencil, sampled, or copy usage");
    }
    if (HasUsage(desc.usage, TextureUsage::Storage))
    {
        if (!capabilities.features.Has(Feature::StorageTextures))
        {
            return Fail(out, ErrorCode::Unsupported,
                        "storage textures are not supported by this device");
        }
        if (!FormatSupportsStorage(desc.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "the texture format cannot be a storage texture");
        }
        if (desc.sampleCount != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a storage texture has exactly one sample");
        }
        if (desc.mipLevels != 1)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a storage texture has exactly one mip level");
        }
    }
    return true;
}

// Usages that require a matching aspect. A color view of a depth format is
// rejected by `ResolveSubresourceRange`, so this only has to check the format.
bool IsViewDimensionAllowed(TextureDimension base, TextureDimension view) noexcept
{
    switch (base)
    {
    case TextureDimension::Dim1D:
        return view == TextureDimension::Dim1D;
    case TextureDimension::Dim2D:
        return view == TextureDimension::Dim2D || view == TextureDimension::Cube;
    case TextureDimension::Dim3D:
        return view == TextureDimension::Dim3D;
    case TextureDimension::Cube:
        return view == TextureDimension::Dim2D || view == TextureDimension::Cube;
    case TextureDimension::Count:
        break;
    }
    return false;
}

bool ValidateTextureView(const TextureInfo& info, const TextureViewDesc& desc,
                         ResolvedRange* resolved, Error* out) noexcept
{
    if (desc.dimension == TextureDimension::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "view dimension is not a dimension");
    }
    if (!IsViewDimensionAllowed(info.dimension, desc.dimension))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "view dimension cannot be exposed by the texture's dimension");
    }
    if (!IsFormatViewCompatible(desc.format, info.format))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "view format is not compatible with the texture format");
    }
    const ResolvedRange range = ResolveSubresourceRange(info, desc.range);
    if (!range.valid)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "view range names a mip, layer, or aspect the texture does not have");
    }
    if (desc.dimension == TextureDimension::Cube)
    {
        if (info.width != info.height)
        {
            return Fail(out, ErrorCode::InvalidArgument, "a cube view requires a square texture");
        }
        if (range.layerCount % 6 != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "a cube view range holds a multiple of six layers");
        }
    }
    if (desc.dimension == TextureDimension::Dim3D && range.layerCount != 1)
    {
        return Fail(out, ErrorCode::InvalidArgument, "a 3d view holds exactly one layer");
    }
    if (resolved != nullptr)
    {
        *resolved = range;
    }
    return true;
}

bool IsFiniteNonNegative(float value, float maximum) noexcept
{
    return value >= 0.0f && value <= maximum;
}

bool ValidateSamplerDesc(const Capabilities& capabilities, const SamplerDesc& desc,
                         Error* out) noexcept
{
    if (desc.minFilter >= FilterMode::Count || desc.magFilter >= FilterMode::Count ||
        desc.mipmapFilter >= FilterMode::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "sampler filter is not a filter");
    }
    if (desc.addressU >= AddressMode::Count || desc.addressV >= AddressMode::Count ||
        desc.addressW >= AddressMode::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "sampler address mode is not an address mode");
    }
    if (desc.borderColor >= BorderColor::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "sampler border color is not a border color");
    }
    if (desc.compareFunction >= CompareFunction::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "sampler comparison is not a comparison function");
    }
    if (desc.maxAnisotropy == 0 || desc.maxAnisotropy > capabilities.limits.maxSamplerAnisotropy)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "sampler anisotropy is outside 1..Limits::maxSamplerAnisotropy");
    }
    if (!IsFiniteNonNegative(desc.minLod, kMaxSamplerLod) ||
        !IsFiniteNonNegative(desc.maxLod, kMaxSamplerLod))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "sampler LOD bounds must be finite, non-negative, and at most 1000");
    }
    if (desc.minLod > desc.maxLod)
    {
        return Fail(out, ErrorCode::InvalidArgument, "sampler minLod is above maxLod");
    }
    if (!(desc.mipLodBias >= -kMaxMipLodBias && desc.mipLodBias <= kMaxMipLodBias))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "sampler LOD bias is outside +/-16 or not finite");
    }
    if (desc.maxAnisotropy > 1)
    {
        if (desc.minFilter != FilterMode::Linear || desc.magFilter != FilterMode::Linear ||
            desc.mipmapFilter != FilterMode::Linear)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "anisotropic filtering requires linear min, mag, and mip filters");
        }
        if (!capabilities.features.Has(Feature::AnisotropicFiltering))
        {
            return Fail(out, ErrorCode::Unsupported,
                        "anisotropic filtering is not supported by this device");
        }
    }
    return true;
}

bool ValidateShaderDesc(const ShaderDesc& desc, Error* out) noexcept
{
    if (desc.stage >= ShaderStage::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "shader stage is not a stage");
    }
    if (desc.format >= ShaderBytecodeFormat::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "shader bytecode format is not a format");
    }
    if (desc.code.Data() == nullptr || desc.code.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument, "shader code is empty");
    }
    const bool sourceCode = desc.format == ShaderBytecodeFormat::HlslSource ||
                            desc.format == ShaderBytecodeFormat::MslSource;
    if (sourceCode && (desc.entryPoint == nullptr || desc.entryPoint[0] == '\0'))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a source shader needs a non-empty entry point");
    }
    return true;
}

bool ValidateBindGroupLayout(const Limits& limits, const BindGroupLayoutDesc& desc,
                             u32* resourceCount, Error* out) noexcept
{
    if (desc.entries.Data() == nullptr || desc.entries.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "a bind group layout needs at least one entry");
    }
    if (desc.entries.Size() > limits.maxBindingsPerGroup)
    {
        return Fail(out, ErrorCode::OutOfRange,
                    "bind group layout has more entries than the device allows");
    }

    u32 total = 0;
    for (usize index = 0; index < desc.entries.Size(); ++index)
    {
        const BindGroupLayoutEntry& entry = desc.entries[index];
        if (entry.type >= BindingType::Count)
        {
            return Fail(out, ErrorCode::InvalidArgument, "binding type is not a binding type");
        }
        if (entry.visibility == 0 || (entry.visibility & ~kAllShaderStages) != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "binding visibility must name at least one real stage");
        }
        if (entry.arrayCount == 0)
        {
            return Fail(out, ErrorCode::InvalidArgument, "binding array count is zero");
        }
        if (entry.binding >= limits.maxBindingsPerGroup)
        {
            return Fail(out, ErrorCode::OutOfRange, "binding number exceeds the device limit");
        }
        if (entry.arrayCount > limits.maxBindingsPerGroup)
        {
            return Fail(out, ErrorCode::OutOfRange, "binding array count exceeds the device limit");
        }
        for (usize other = 0; other < index; ++other)
        {
            if (desc.entries[other].binding == entry.binding)
            {
                return Fail(out, ErrorCode::InvalidArgument,
                            "bind group layout repeats a binding number");
            }
        }
        total += entry.arrayCount;
    }

    if (resourceCount != nullptr)
    {
        *resourceCount = total;
    }
    return true;
}

bool ValidatePipelineLayout(const Capabilities& capabilities, const Existence& live,
                            const PipelineLayoutDesc& desc, Error* out) noexcept
{
    u32 usedSlots = 0;
    for (u32 slot = 0; slot < kBindGroupSlotCount; ++slot)
    {
        if (!desc.groups[slot].IsValid())
        {
            continue;
        }
        if (!live.bindGroupLayout(live.tables, desc.groups[slot]))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "pipeline layout names a stale group layout");
        }
        for (u32 other = 0; other < slot; ++other)
        {
            if (desc.groups[other] == desc.groups[slot])
            {
                return Fail(out, ErrorCode::InvalidArgument,
                            "pipeline layout repeats a group layout");
            }
        }
        ++usedSlots;
    }
    if (usedSlots == 0)
    {
        return Fail(out, ErrorCode::InvalidArgument, "pipeline layout uses no group slot");
    }

    if (desc.pushConstants.Size() > kMaxPushConstantRanges)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "pipeline layout declares too many push constant ranges");
    }
    for (usize index = 0; index < desc.pushConstants.Size(); ++index)
    {
        const PushConstantRange& range = desc.pushConstants[index];
        if (range.stages == 0 || (range.stages & ~kAllShaderStages) != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "push constant range must name at least one real stage");
        }
        if (range.sizeBytes == 0 || range.offsetBytes % 4 != 0 || range.sizeBytes % 4 != 0)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "push constant range is empty or not four byte aligned");
        }
        if (range.offsetBytes > capabilities.limits.maxPushConstantBytes ||
            range.sizeBytes > capabilities.limits.maxPushConstantBytes - range.offsetBytes)
        {
            return Fail(out, ErrorCode::OutOfRange, "push constant range exceeds the device limit");
        }
        for (usize other = 0; other < index; ++other)
        {
            const PushConstantRange& previous = desc.pushConstants[other];
            if ((previous.stages & range.stages) == 0)
            {
                continue;
            }
            const u32 previousEnd = previous.offsetBytes + previous.sizeBytes;
            const u32 end = range.offsetBytes + range.sizeBytes;
            if (range.offsetBytes < previousEnd && previous.offsetBytes < end)
            {
                return Fail(out, ErrorCode::InvalidArgument,
                            "push constant ranges overlap for a shared stage");
            }
        }
    }
    return true;
}

bool ValidateVertexLayout(const Capabilities& capabilities, const VertexLayout& layout,
                          Error* out) noexcept
{
    if (layout.bindings.Data() == nullptr || layout.bindings.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument, "graphics pipeline has no vertex binding");
    }
    if (layout.attributes.Data() == nullptr || layout.attributes.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument, "graphics pipeline has no vertex attribute");
    }
    if (layout.bindings.Size() > capabilities.limits.maxVertexBuffers)
    {
        return Fail(out, ErrorCode::OutOfRange,
                    "graphics pipeline has more vertex bindings than the device allows");
    }
    if (layout.attributes.Size() > capabilities.limits.maxVertexAttributes)
    {
        return Fail(out, ErrorCode::OutOfRange,
                    "graphics pipeline has more vertex attributes than the device allows");
    }

    for (usize index = 0; index < layout.bindings.Size(); ++index)
    {
        const VertexBinding& binding = layout.bindings[index];
        if (binding.strideBytes == 0 || binding.strideBytes > kMaxVertexStrideBytes)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "vertex binding stride is zero or above 2048 bytes");
        }
        if (binding.rate >= VertexInputRate::Count)
        {
            return Fail(out, ErrorCode::InvalidArgument, "vertex input rate is not an input rate");
        }
    }

    for (usize index = 0; index < layout.attributes.Size(); ++index)
    {
        const VertexAttribute& attribute = layout.attributes[index];
        if (!IsVertexFormatSupported(attribute.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "vertex attribute format cannot be fetched as vertex input");
        }
        if (attribute.binding >= layout.bindings.Size())
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "vertex attribute names a binding that does not exist");
        }
        const u64 end =
            static_cast<u64>(attribute.offsetBytes) + FormatBytesPerBlock(attribute.format);
        if (end > layout.bindings[attribute.binding].strideBytes)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "vertex attribute extends past its binding stride");
        }
        for (usize other = 0; other < index; ++other)
        {
            if (layout.attributes[other].location == attribute.location)
            {
                return Fail(out, ErrorCode::InvalidArgument,
                            "vertex layout repeats an attribute location");
            }
        }
    }
    return true;
}

bool ValidateBlendState(Format format, const BlendState& blend, Error* out) noexcept
{
    if (blend.writeMask > kColorWriteAll)
    {
        return Fail(out, ErrorCode::InvalidArgument, "blend write mask has bits above alpha");
    }
    if (!blend.enabled)
    {
        return true;
    }
    if (!FormatSupportsBlending(format))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "blending is not supported by the attachment format");
    }
    const BlendComponent* components[2] = {&blend.color, &blend.alpha};
    for (const BlendComponent* component : components)
    {
        if (component->source >= BlendFactor::Count ||
            component->destination >= BlendFactor::Count ||
            component->operation >= BlendOperation::Count)
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "blend factor or operation is not an enumerator");
        }
    }
    return true;
}

bool ValidateGraphicsPipelineDesc(const Capabilities& capabilities, const Existence& live,
                                  const GraphicsPipelineDesc& desc, Error* out) noexcept
{
    if (!live.pipelineLayout(live.tables, desc.layout))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline names a stale pipeline layout");
    }
    if (live.shaderStage(live.tables, desc.vertexShader) == ShaderStage::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline names a stale vertex shader");
    }
    if (live.shaderStage(live.tables, desc.vertexShader) != ShaderStage::Vertex)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline vertex shader is not a vertex stage");
    }
    if (live.shaderStage(live.tables, desc.pixelShader) == ShaderStage::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline names a stale pixel shader");
    }
    if (live.shaderStage(live.tables, desc.pixelShader) != ShaderStage::Pixel)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "graphics pipeline pixel shader is not a pixel stage");
    }
    if (desc.cache.IsValid() && !live.cache(live.tables, desc.cache))
    {
        return Fail(out, ErrorCode::InvalidArgument, "graphics pipeline names a stale cache");
    }
    if (!ValidateVertexLayout(capabilities, desc.vertexLayout, out))
    {
        return false;
    }
    if (desc.topology >= PrimitiveTopology::Count || desc.rasterizer.cullMode >= CullMode::Count ||
        desc.rasterizer.frontFace >= FrontFace::Count ||
        desc.rasterizer.polygonMode >= PolygonMode::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "rasterizer or topology state is invalid");
    }
    if (desc.multisample.sampleCount != 1 && desc.multisample.sampleCount != 2 &&
        desc.multisample.sampleCount != 4 && desc.multisample.sampleCount != 8)
    {
        return Fail(out, ErrorCode::InvalidArgument, "pipeline sample count must be 1, 2, 4, or 8");
    }
    if (desc.multisample.alphaToCoverage && desc.multisample.sampleCount == 1)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "alpha to coverage needs more than one sample");
    }
    if (desc.depthStencil.depthCompare >= CompareFunction::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument, "depth comparison is not a comparison");
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
        return Fail(out, ErrorCode::InvalidArgument, "stencil state has an invalid enumerator");
    }

    const bool needsDepth = desc.depthStencil.depthTestEnabled ||
                            desc.depthStencil.depthWriteEnabled || desc.depthStencil.stencilEnabled;
    if (needsDepth && desc.depthStencilFormat == Format::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "pipeline has depth state but no depth attachment format");
    }
    if (desc.depthStencilFormat != Format::Count)
    {
        if (!IsDepthFormat(desc.depthStencilFormat))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "depth attachment format is not a depth format");
        }
        if (desc.depthStencil.stencilEnabled && !IsStencilFormat(desc.depthStencilFormat))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "stencil state needs a depth format with stencil");
        }
        if (desc.multisample.sampleCount > 1 && !FormatSupportsMultisample(desc.depthStencilFormat))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "depth attachment format cannot be multisampled");
        }
    }

    if (desc.colorAttachments.Data() == nullptr || desc.colorAttachments.Empty())
    {
        return Fail(out, ErrorCode::InvalidArgument, "graphics pipeline has no color attachment");
    }
    if (desc.colorAttachments.Size() > kMaxColorAttachments ||
        desc.colorAttachments.Size() > capabilities.limits.maxColorAttachments)
    {
        return Fail(out, ErrorCode::OutOfRange,
                    "graphics pipeline has more color attachments than the device allows");
    }
    for (usize index = 0; index < desc.colorAttachments.Size(); ++index)
    {
        const ColorAttachmentState& attachment = desc.colorAttachments[index];
        if (!FormatSupportsRenderTarget(attachment.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "color attachment format cannot be a render target");
        }
        if (desc.multisample.sampleCount > 1 && !FormatSupportsMultisample(attachment.format))
        {
            return Fail(out, ErrorCode::InvalidArgument,
                        "color attachment format cannot be multisampled");
        }
        if (!ValidateBlendState(attachment.format, attachment.blend, out))
        {
            return false;
        }
    }
    return true;
}

bool ValidateComputePipelineDesc(const Capabilities& capabilities, const Existence& live,
                                 const ComputePipelineDesc& desc, Error* out) noexcept
{
    if (!capabilities.features.Has(Feature::Compute))
    {
        return Fail(out, ErrorCode::Unsupported, "compute pipelines are not supported");
    }
    if (!live.pipelineLayout(live.tables, desc.layout))
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "compute pipeline names a stale pipeline layout");
    }
    if (live.shaderStage(live.tables, desc.computeShader) == ShaderStage::Count)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "compute pipeline names a stale compute shader");
    }
    if (live.shaderStage(live.tables, desc.computeShader) != ShaderStage::Compute)
    {
        return Fail(out, ErrorCode::InvalidArgument,
                    "compute pipeline shader is not a compute stage");
    }
    if (desc.cache.IsValid() && !live.cache(live.tables, desc.cache))
    {
        return Fail(out, ErrorCode::InvalidArgument, "compute pipeline names a stale cache");
    }
    return true;
}

}  // namespace cyber::rhi::validation
