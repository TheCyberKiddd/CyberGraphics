#include <cyber/renderer/reflected_layout.h>

namespace cyber::renderer
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::usize;
using cyber::memory::Span;
using cyber::memory::StringView;
using shaders::Reflection;
using shaders::ResourceBinding;
using shaders::ResourceKind;

[[nodiscard]] Result<ReflectedLayout, Error> LayoutError(ErrorCode code,
                                                         const char* message) noexcept
{
    return Result<ReflectedLayout, Error>::Err(Error{code, message});
}

// One resource seen in any stage, keyed by kind, space, and register.
struct Seen
{
    ResourceKind kind = ResourceKind::SampledTexture;
    u32 space = 0;
    u32 registerIndex = 0;
    rhi::ShaderStageMask visibility = 0;
    shaders::ReflectedName name{};
};

[[nodiscard]] bool SameName(const shaders::ReflectedName& lhs,
                            const shaders::ReflectedName& rhs) noexcept
{
    return lhs.View() == rhs.View();
}

[[nodiscard]] rhi::BindingType BindingTypeOf(ResourceKind kind) noexcept
{
    return kind == ResourceKind::Sampler ? rhi::BindingType::Sampler
                                         : rhi::BindingType::SampledTexture;
}

}  // namespace

rhi::ShaderStage ToRhiStage(shaders::Stage stage) noexcept
{
    switch (stage)
    {
    case shaders::Stage::Vertex:
        return rhi::ShaderStage::Vertex;
    case shaders::Stage::Pixel:
        return rhi::ShaderStage::Pixel;
    case shaders::Stage::Compute:
        return rhi::ShaderStage::Compute;
    }
    return rhi::ShaderStage::Count;
}

u32 ReflectedLayout::FindBinding(rhi::BindGroupSlot slot, StringView name) const noexcept
{
    const u32 index = static_cast<u32>(slot);
    if (index >= rhi::kBindGroupSlotCount)
    {
        return kNoBinding;
    }
    for (u32 binding = 0; binding < bindingCounts[index]; ++binding)
    {
        if (slots[index][binding].name.View() == name)
        {
            return slots[index][binding].entry.binding;
        }
    }
    return kNoBinding;
}

bool operator==(const ReflectedLayout& lhs, const ReflectedLayout& rhs) noexcept
{
    if (lhs.pushConstants.offsetBytes != rhs.pushConstants.offsetBytes ||
        lhs.pushConstants.sizeBytes != rhs.pushConstants.sizeBytes ||
        lhs.pushConstants.stages != rhs.pushConstants.stages)
    {
        return false;
    }
    for (u32 slot = 0; slot < rhi::kBindGroupSlotCount; ++slot)
    {
        if (lhs.bindingCounts[slot] != rhs.bindingCounts[slot])
        {
            return false;
        }
        for (u32 binding = 0; binding < lhs.bindingCounts[slot]; ++binding)
        {
            const ReflectedBinding& a = lhs.slots[slot][binding];
            const ReflectedBinding& b = rhs.slots[slot][binding];
            if (a.entry.binding != b.entry.binding || a.entry.type != b.entry.type ||
                a.entry.visibility != b.entry.visibility ||
                a.entry.arrayCount != b.entry.arrayCount || a.registerIndex != b.registerIndex ||
                !SameName(a.name, b.name))
            {
                return false;
            }
        }
    }
    return true;
}

Result<ReflectedLayout, Error> BuildReflectedLayout(Span<const Reflection* const> stages) noexcept
{
    constexpr usize kMaximumSeen = rhi::kBindGroupSlotCount * kMaxReflectedBindingsPerSlot;
    std::array<Seen, kMaximumSeen> seen{};
    usize seenCount = 0;
    ReflectedLayout layout{};
    rhi::ShaderStageMask stagesSeen = 0;

    for (usize stageIndex = 0; stageIndex < stages.Size(); ++stageIndex)
    {
        const Reflection* reflection = stages[stageIndex];
        if (reflection == nullptr)
        {
            continue;
        }
        const rhi::ShaderStage stage = ToRhiStage(reflection->stage);
        const rhi::ShaderStageMask bit = rhi::ShaderStageBit(stage);
        if (bit == 0 || (stagesSeen & bit) != 0)
        {
            return LayoutError(ErrorCode::InvalidArgument,
                               "reflected layout: each stage may appear once");
        }
        stagesSeen |= bit;

        for (u32 index = 0; index < reflection->resourceCount && index < shaders::kMaxResources;
             ++index)
        {
            const ResourceBinding& resource = reflection->resources[index];
            if (resource.kind == ResourceKind::ConstantBuffer)
            {
                if (resource.registerIndex != 0 || resource.registerSpace != 0)
                {
                    return LayoutError(ErrorCode::Unsupported,
                                       "reflected layout: only the b0/space0 constant buffer is "
                                       "bindable until constant streaming (Phase 19)");
                }
                if (resource.sizeBytes > kMaxReflectedPushConstantBytes)
                {
                    return LayoutError(ErrorCode::Unsupported,
                                       "reflected layout: the b0 push-constant block exceeds 128 "
                                       "bytes");
                }
                if (resource.sizeBytes > layout.pushConstants.sizeBytes)
                {
                    layout.pushConstants.sizeBytes = resource.sizeBytes;
                }
                layout.pushConstants.stages |= bit;
                continue;
            }
            if (resource.kind == ResourceKind::StorageTexture)
            {
                return LayoutError(ErrorCode::Unsupported,
                                   "reflected layout: storage textures need UAV descriptor ranges, "
                                   "which the D3D12 backend does not build yet");
            }
            if (resource.registerSpace >= rhi::kBindGroupSlotCount)
            {
                return LayoutError(ErrorCode::Unsupported,
                                   "reflected layout: register space beyond the four bind-group "
                                   "slots");
            }

            bool merged = false;
            for (usize prior = 0; prior < seenCount; ++prior)
            {
                Seen& other = seen[prior];
                const bool sameClass = (other.kind == ResourceKind::Sampler) ==
                                       (resource.kind == ResourceKind::Sampler);
                if (other.space == resource.registerSpace &&
                    other.registerIndex == resource.registerIndex && sameClass)
                {
                    if (other.kind != resource.kind)
                    {
                        return LayoutError(ErrorCode::InvalidArgument,
                                           "reflected layout: a register is used by two resource "
                                           "kinds");
                    }
                    other.visibility |= bit;
                    merged = true;
                    break;
                }
            }
            if (merged)
            {
                continue;
            }
            if (seenCount >= seen.size())
            {
                return LayoutError(ErrorCode::Unsupported,
                                   "reflected layout: binding limit exceeded");
            }
            seen[seenCount++] = Seen{resource.kind, resource.registerSpace, resource.registerIndex,
                                     bit, resource.name};
        }
    }

    // Lay out each slot: sampled textures in register order, then samplers.
    for (u32 slot = 0; slot < rhi::kBindGroupSlotCount; ++slot)
    {
        for (u32 pass = 0; pass < 2; ++pass)
        {
            const bool wantSampler = pass == 1;
            u32 expected = 0;
            for (;;)
            {
                const Seen* next = nullptr;
                u32 count = 0;
                for (usize index = 0; index < seenCount; ++index)
                {
                    const Seen& candidate = seen[index];
                    if (candidate.space != slot ||
                        (candidate.kind == ResourceKind::Sampler) != wantSampler)
                    {
                        continue;
                    }
                    ++count;
                    if (candidate.registerIndex == expected)
                    {
                        next = &candidate;
                    }
                }
                if (next == nullptr)
                {
                    if (count != expected)
                    {
                        return LayoutError(ErrorCode::Unsupported,
                                           "reflected layout: registers in a slot must be "
                                           "contiguous from 0");
                    }
                    break;
                }
                if (layout.bindingCounts[slot] >= kMaxReflectedBindingsPerSlot)
                {
                    return LayoutError(ErrorCode::Unsupported,
                                       "reflected layout: too many bindings in one slot");
                }
                ReflectedBinding& binding = layout.slots[slot][layout.bindingCounts[slot]];
                binding.entry.binding = layout.bindingCounts[slot];
                binding.entry.type = BindingTypeOf(next->kind);
                binding.entry.visibility = next->visibility;
                binding.entry.arrayCount = 1;
                binding.name = next->name;
                binding.registerIndex = next->registerIndex;
                ++layout.bindingCounts[slot];
                ++expected;
            }
        }
    }
    return Result<ReflectedLayout, Error>::Ok(layout);
}

rhi::VertexLayout ReflectedVertexLayout::View() const noexcept
{
    rhi::VertexLayout view{};
    view.bindings = Span<const rhi::VertexBinding>(&binding, 1);
    view.attributes = Span<const rhi::VertexAttribute>(attributes.data(), attributeCount);
    return view;
}

Result<ReflectedVertexLayout, Error> BuildVertexLayout(const Reflection& vertex) noexcept
{
    if (vertex.stage != shaders::Stage::Vertex || vertex.inputCount > kMaxReflectedVertexAttributes)
    {
        return Result<ReflectedVertexLayout, Error>::Err(
            Error{ErrorCode::InvalidArgument, "vertex layout: reflection is not a vertex stage"});
    }
    ReflectedVertexLayout layout{};
    // Inputs in ascending location order; locations are unique per the compiler.
    u32 previous = 0;
    bool first = true;
    u32 offset = 0;
    for (u32 placed = 0; placed < vertex.inputCount; ++placed)
    {
        const shaders::StageVariable* next = nullptr;
        for (u32 index = 0; index < vertex.inputCount; ++index)
        {
            const shaders::StageVariable& input = vertex.inputs[index];
            if ((first || input.location > previous) &&
                (next == nullptr || input.location < next->location))
            {
                next = &input;
            }
        }
        if (next == nullptr)
        {
            break;
        }
        rhi::Format format = rhi::Format::Count;
        switch (next->type)
        {
        case shaders::ValueType::Float:
            format = rhi::Format::R32Float;
            break;
        case shaders::ValueType::Float2:
            format = rhi::Format::RG32Float;
            break;
        case shaders::ValueType::Float3:
            format = rhi::Format::R32G32B32Float;
            break;
        case shaders::ValueType::Float4:
            format = rhi::Format::RGBA32Float;
            break;
        default:
            return Result<ReflectedVertexLayout, Error>::Err(Error{
                ErrorCode::InvalidArgument, "vertex layout: input type has no vertex format"});
        }
        rhi::VertexAttribute& attribute = layout.attributes[layout.attributeCount++];
        attribute.location = next->location;
        attribute.binding = 0;
        attribute.format = format;
        attribute.offsetBytes = offset;
        offset += shaders::ValueTypeSize(next->type);
        previous = next->location;
        first = false;
    }
    layout.binding.strideBytes = offset;
    layout.binding.rate = rhi::VertexInputRate::PerVertex;
    return Result<ReflectedVertexLayout, Error>::Ok(layout);
}

Result<ReflectedLayoutObjects, Error> CreateReflectedLayoutObjects(rhi::Device& device,
                                                                   const ReflectedLayout& layout,
                                                                   const char* label) noexcept
{
    ReflectedLayoutObjects objects{};
    for (u32 slot = 0; slot < rhi::kBindGroupSlotCount; ++slot)
    {
        if (layout.bindingCounts[slot] == 0)
        {
            continue;
        }
        std::array<rhi::BindGroupLayoutEntry, kMaxReflectedBindingsPerSlot> entries{};
        for (u32 binding = 0; binding < layout.bindingCounts[slot]; ++binding)
        {
            entries[binding] = layout.slots[slot][binding].entry;
        }
        rhi::BindGroupLayoutDesc desc{};
        desc.entries =
            Span<const rhi::BindGroupLayoutEntry>(entries.data(), layout.bindingCounts[slot]);
        desc.label = label;
        auto created = device.CreateBindGroupLayout(desc);
        if (!created)
        {
            DestroyReflectedLayoutObjects(device, objects);
            return Result<ReflectedLayoutObjects, Error>::Err(created.Error());
        }
        objects.groups[slot] = created.Value();
    }

    rhi::PipelineLayoutDesc desc{};
    for (u32 slot = 0; slot < rhi::kBindGroupSlotCount; ++slot)
    {
        desc.groups[slot] = objects.groups[slot];
    }
    if (layout.pushConstants.sizeBytes > 0)
    {
        desc.pushConstants = Span<const rhi::PushConstantRange>(&layout.pushConstants, 1);
    }
    desc.label = label;
    auto created = device.CreatePipelineLayout(desc);
    if (!created)
    {
        DestroyReflectedLayoutObjects(device, objects);
        return Result<ReflectedLayoutObjects, Error>::Err(created.Error());
    }
    objects.pipelineLayout = created.Value();
    return Result<ReflectedLayoutObjects, Error>::Ok(objects);
}

void DestroyReflectedLayoutObjects(rhi::Device& device, ReflectedLayoutObjects& objects) noexcept
{
    if (objects.pipelineLayout.IsValid())
    {
        static_cast<void>(device.DestroyPipelineLayout(objects.pipelineLayout));
    }
    for (u32 slot = 0; slot < rhi::kBindGroupSlotCount; ++slot)
    {
        if (objects.groups[slot].IsValid())
        {
            static_cast<void>(device.DestroyBindGroupLayout(objects.groups[slot]));
        }
    }
    objects = ReflectedLayoutObjects{};
}

}  // namespace cyber::renderer
