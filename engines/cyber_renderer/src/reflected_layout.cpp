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
    u32 sizeBytes = 0;
    const Reflection* reflection = nullptr;
    const ResourceBinding* resource = nullptr;
};

[[nodiscard]] bool SameConstantBuffer(const Reflection& lhsReflection, const ResourceBinding& lhs,
                                      const Reflection& rhsReflection,
                                      const ResourceBinding& rhs) noexcept
{
    if (lhs.sizeBytes != rhs.sizeBytes || lhs.memberCount != rhs.memberCount ||
        lhs.firstMember > lhsReflection.memberCount ||
        rhs.firstMember > rhsReflection.memberCount ||
        lhs.memberCount > lhsReflection.memberCount - lhs.firstMember ||
        rhs.memberCount > rhsReflection.memberCount - rhs.firstMember)
    {
        return false;
    }
    for (u32 member = 0; member < lhs.memberCount; ++member)
    {
        const shaders::ConstantMember& a = lhsReflection.members[lhs.firstMember + member];
        const shaders::ConstantMember& b = rhsReflection.members[rhs.firstMember + member];
        if (a.name.View() != b.name.View() || a.type != b.type || a.offsetBytes != b.offsetBytes ||
            a.sizeBytes != b.sizeBytes)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SameRegisterClass(ResourceKind lhs, ResourceKind rhs) noexcept
{
    if (lhs == ResourceKind::ConstantBuffer || rhs == ResourceKind::ConstantBuffer)
    {
        return lhs == rhs;
    }
    return (lhs == ResourceKind::Sampler) == (rhs == ResourceKind::Sampler);
}

[[nodiscard]] bool SameName(const shaders::ReflectedName& lhs,
                            const shaders::ReflectedName& rhs) noexcept
{
    return lhs.View() == rhs.View();
}

[[nodiscard]] rhi::BindingType BindingTypeOf(ResourceKind kind) noexcept
{
    switch (kind)
    {
    case ResourceKind::ConstantBuffer:
        return rhi::BindingType::UniformBuffer;
    case ResourceKind::Sampler:
        return rhi::BindingType::Sampler;
    case ResourceKind::SampledTexture:
        return rhi::BindingType::SampledTexture;
    case ResourceKind::StorageTexture:
        break;
    }
    return rhi::BindingType::Count;
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
        lhs.pushConstants.stages != rhs.pushConstants.stages ||
        lhs.materialConstantsBinding != rhs.materialConstantsBinding ||
        lhs.materialConstantsSizeBytes != rhs.materialConstantsSizeBytes ||
        lhs.materialConstantsMemberCount != rhs.materialConstantsMemberCount)
    {
        return false;
    }
    for (u32 member = 0; member < lhs.materialConstantsMemberCount; ++member)
    {
        const shaders::ConstantMember& a = lhs.materialConstantsMembers[member];
        const shaders::ConstantMember& b = rhs.materialConstantsMembers[member];
        if (!SameName(a.name, b.name) || a.type != b.type || a.offsetBytes != b.offsetBytes ||
            a.sizeBytes != b.sizeBytes)
        {
            return false;
        }
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
    if (stages.Data() == nullptr && !stages.Empty())
    {
        return LayoutError(ErrorCode::InvalidArgument,
                           "reflected layout: non-empty stage span has a null pointer");
    }
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
        if (reflection->resourceCount > shaders::kMaxResources ||
            reflection->memberCount > shaders::kMaxConstantMembers)
        {
            return LayoutError(ErrorCode::InvalidArgument,
                               "reflected layout: reflection counts exceed their fixed bounds");
        }
        stagesSeen |= bit;

        for (u32 index = 0; index < reflection->resourceCount; ++index)
        {
            const ResourceBinding& resource = reflection->resources[index];
            if (resource.kind != ResourceKind::ConstantBuffer &&
                resource.kind != ResourceKind::SampledTexture &&
                resource.kind != ResourceKind::Sampler &&
                resource.kind != ResourceKind::StorageTexture)
            {
                return LayoutError(ErrorCode::InvalidArgument,
                                   "reflected layout: unknown resource kind");
            }
            if (resource.kind == ResourceKind::ConstantBuffer)
            {
                if (resource.registerIndex == 0 && resource.registerSpace == 0)
                {
                    if (resource.sizeBytes > kMaxReflectedPushConstantBytes)
                    {
                        return LayoutError(ErrorCode::Unsupported,
                                           "reflected layout: the b0 push-constant block exceeds "
                                           "128 bytes");
                    }
                    if (resource.sizeBytes > layout.pushConstants.sizeBytes)
                    {
                        layout.pushConstants.sizeBytes = resource.sizeBytes;
                    }
                    layout.pushConstants.stages |= bit;
                    continue;
                }
                if (resource.registerIndex != 1 || resource.registerSpace != 2)
                {
                    return LayoutError(ErrorCode::Unsupported,
                                       "reflected layout: material constants must use b1, space2");
                }
                if (resource.sizeBytes == 0 ||
                    resource.sizeBytes > kMaxReflectedMaterialConstantBytes ||
                    resource.memberCount == 0 || resource.firstMember > reflection->memberCount ||
                    resource.memberCount > reflection->memberCount - resource.firstMember)
                {
                    return LayoutError(ErrorCode::Unsupported,
                                       "reflected layout: invalid or oversized material constant "
                                       "buffer");
                }
                if (layout.materialConstantsMemberCount == 0)
                {
                    layout.materialConstantsMemberCount = resource.memberCount;
                    for (u32 member = 0; member < resource.memberCount; ++member)
                    {
                        layout.materialConstantsMembers[member] =
                            reflection->members[resource.firstMember + member];
                    }
                }
                else
                {
                    if (layout.materialConstantsMemberCount != resource.memberCount)
                    {
                        return LayoutError(
                            ErrorCode::InvalidArgument,
                            "reflected layout: material member counts differ between stages");
                    }
                    for (u32 member = 0; member < resource.memberCount; ++member)
                    {
                        const shaders::ConstantMember& prior =
                            layout.materialConstantsMembers[member];
                        const shaders::ConstantMember& current =
                            reflection->members[resource.firstMember + member];
                        if (!SameName(prior.name, current.name) || prior.type != current.type ||
                            prior.offsetBytes != current.offsetBytes ||
                            prior.sizeBytes != current.sizeBytes)
                        {
                            return LayoutError(
                                ErrorCode::InvalidArgument,
                                "reflected layout: material members differ between stages");
                        }
                    }
                }
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
                const bool sameClass = SameRegisterClass(other.kind, resource.kind);
                if (other.space == resource.registerSpace &&
                    other.registerIndex == resource.registerIndex && sameClass)
                {
                    if (other.kind != resource.kind)
                    {
                        return LayoutError(ErrorCode::InvalidArgument,
                                           "reflected layout: a register is used by two resource "
                                           "kinds");
                    }
                    if (resource.kind == ResourceKind::ConstantBuffer &&
                        (other.reflection == nullptr || other.resource == nullptr ||
                         !SameConstantBuffer(*other.reflection, *other.resource, *reflection,
                                             resource)))
                    {
                        return LayoutError(ErrorCode::InvalidArgument,
                                           "reflected layout: material constant-buffer sizes "
                                           "differ between stages");
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
            seen[seenCount++] = Seen{resource.kind,
                                     resource.registerSpace,
                                     resource.registerIndex,
                                     bit,
                                     resource.name,
                                     resource.sizeBytes,
                                     reflection,
                                     &resource};
        }
    }

    // Lay out each slot: uniform buffers, sampled textures, then samplers.
    for (u32 slot = 0; slot < rhi::kBindGroupSlotCount; ++slot)
    {
        for (u32 pass = 0; pass < 3; ++pass)
        {
            const ResourceKind wantKind =
                pass == 0 ? ResourceKind::ConstantBuffer
                          : (pass == 1 ? ResourceKind::SampledTexture : ResourceKind::Sampler);
            const u32 firstRegister = wantKind == ResourceKind::ConstantBuffer && slot == 2 ? 1 : 0;
            u32 expected = firstRegister;
            for (;;)
            {
                const Seen* next = nullptr;
                u32 count = 0;
                for (usize index = 0; index < seenCount; ++index)
                {
                    const Seen& candidate = seen[index];
                    if (candidate.space != slot || candidate.kind != wantKind)
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
                    if (count != expected - firstRegister)
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
                binding.entry.shaderRegister = next->registerIndex;
                binding.name = next->name;
                binding.registerIndex = next->registerIndex;
                if (next->kind == ResourceKind::ConstantBuffer)
                {
                    if (layout.materialConstantsBinding != kNoBinding)
                    {
                        return LayoutError(
                            ErrorCode::Unsupported,
                            "reflected layout: only one material constant buffer is supported");
                    }
                    layout.materialConstantsBinding = binding.entry.binding;
                    layout.materialConstantsSizeBytes = next->sizeBytes;
                }
                ++layout.bindingCounts[slot];
                ++expected;
                if (wantKind == ResourceKind::ConstantBuffer)
                {
                    if (count != 1)
                    {
                        return LayoutError(
                            ErrorCode::Unsupported,
                            "reflected layout: only one material constant buffer is supported");
                    }
                    break;
                }
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
