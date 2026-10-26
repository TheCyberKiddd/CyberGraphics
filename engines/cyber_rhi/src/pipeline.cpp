// Stable hashes for pipeline descriptions.
//
// A pipeline cache is only meaningful if "the same pipeline" is well defined, so
// these functions hash every field that changes the compiled object and nothing
// else. Labels are excluded on purpose: renaming a pipeline must not invalidate
// a cache entry.

#include <cyber/core/hash.h>
#include <cyber/rhi/pipeline.h>

#include <cstring>

namespace cyber::rhi
{

using cyber::core::usize;

namespace
{

// Distinct seeds so a graphics and a compute description that happen to share a
// field sequence cannot collide by construction.
inline constexpr u64 kGraphicsSeed = 0x9E3779B97F4A7C15ULL;
inline constexpr u64 kComputeSeed = 0xD1B54A32D192ED03ULL;

[[nodiscard]] u64 Mix(u64 seed, u64 value) noexcept
{
    return cyber::core::HashCombine(seed, value);
}

[[nodiscard]] u64 MixBool(u64 seed, bool value) noexcept
{
    return Mix(seed, value ? 1ULL : 0ULL);
}

[[nodiscard]] u64 MixFloat(u64 seed, float value) noexcept
{
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return Mix(seed, bits);
}

template <typename Enum>
[[nodiscard]] u64 MixEnum(u64 seed, Enum value) noexcept
{
    return Mix(seed, static_cast<u64>(value));
}

[[nodiscard]] u64 MixBlendComponent(u64 seed, const BlendComponent& component) noexcept
{
    seed = MixEnum(seed, component.source);
    seed = MixEnum(seed, component.destination);
    return MixEnum(seed, component.operation);
}

[[nodiscard]] u64 MixBlendState(u64 seed, const BlendState& blend) noexcept
{
    seed = MixBool(seed, blend.enabled);
    seed = MixBlendComponent(seed, blend.color);
    seed = MixBlendComponent(seed, blend.alpha);
    return Mix(seed, blend.writeMask);
}

[[nodiscard]] u64 MixStencilFace(u64 seed, const StencilFaceState& face) noexcept
{
    seed = MixEnum(seed, face.compare);
    seed = MixEnum(seed, face.failOp);
    seed = MixEnum(seed, face.depthFailOp);
    seed = MixEnum(seed, face.passOp);
    seed = Mix(seed, face.readMask);
    return Mix(seed, face.writeMask);
}

[[nodiscard]] u64 MixVertexLayout(u64 seed, const VertexLayout& layout) noexcept
{
    seed = Mix(seed, layout.bindings.Size());
    for (usize index = 0; index < layout.bindings.Size(); ++index)
    {
        const VertexBinding& binding = layout.bindings[index];
        seed = Mix(seed, binding.strideBytes);
        seed = MixEnum(seed, binding.rate);
    }
    seed = Mix(seed, layout.attributes.Size());
    for (usize index = 0; index < layout.attributes.Size(); ++index)
    {
        const VertexAttribute& attribute = layout.attributes[index];
        seed = Mix(seed, attribute.location);
        seed = Mix(seed, attribute.binding);
        seed = MixEnum(seed, attribute.format);
        seed = Mix(seed, attribute.offsetBytes);
    }
    return seed;
}

}  // namespace

u64 HashGraphicsPipelineDesc(const GraphicsPipelineDesc& desc) noexcept
{
    u64 seed = kGraphicsSeed;
    seed = Mix(seed, PackHandle(desc.layout));
    seed = Mix(seed, PackHandle(desc.vertexShader));
    seed = Mix(seed, PackHandle(desc.pixelShader));
    seed = MixVertexLayout(seed, desc.vertexLayout);

    seed = MixEnum(seed, desc.topology);

    seed = MixEnum(seed, desc.rasterizer.cullMode);
    seed = MixEnum(seed, desc.rasterizer.frontFace);
    seed = MixEnum(seed, desc.rasterizer.polygonMode);
    seed = MixBool(seed, desc.rasterizer.depthClipEnabled);
    seed = MixFloat(seed, desc.rasterizer.depthBiasConstant);
    seed = MixFloat(seed, desc.rasterizer.depthBiasSlope);
    seed = MixFloat(seed, desc.rasterizer.depthBiasClamp);

    seed = Mix(seed, desc.multisample.sampleCount);
    seed = Mix(seed, desc.multisample.sampleMask);
    seed = MixBool(seed, desc.multisample.alphaToCoverage);

    seed = MixBool(seed, desc.depthStencil.depthTestEnabled);
    seed = MixBool(seed, desc.depthStencil.depthWriteEnabled);
    seed = MixEnum(seed, desc.depthStencil.depthCompare);
    seed = MixBool(seed, desc.depthStencil.stencilEnabled);
    seed = MixStencilFace(seed, desc.depthStencil.front);
    seed = MixStencilFace(seed, desc.depthStencil.back);
    seed = MixEnum(seed, desc.depthStencilFormat);

    seed = Mix(seed, desc.colorAttachments.Size());
    for (usize index = 0; index < desc.colorAttachments.Size(); ++index)
    {
        const ColorAttachmentState& attachment = desc.colorAttachments[index];
        seed = MixEnum(seed, attachment.format);
        seed = MixBlendState(seed, attachment.blend);
    }

    // The cache handle is deliberately absent: a cache changes how fast a
    // pipeline compiles, never what it is.
    return seed;
}

u64 HashComputePipelineDesc(const ComputePipelineDesc& desc) noexcept
{
    u64 seed = kComputeSeed;
    seed = Mix(seed, PackHandle(desc.layout));
    seed = Mix(seed, PackHandle(desc.computeShader));
    return seed;
}

}  // namespace cyber::rhi
