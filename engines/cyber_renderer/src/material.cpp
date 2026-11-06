#include <cyber/renderer/material.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace cyber::renderer
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::memory::Span;
using cyber::memory::StringView;

[[nodiscard]] Error MaterialError(ErrorCode code, const char* message) noexcept
{
    return Error{code, message};
}

[[nodiscard]] bool IsIdentifier(StringView value) noexcept
{
    if (value.Data() == nullptr || value.Size() == 0 || value.Size() > kMaxMaterialNameBytes)
    {
        return false;
    }
    const auto first = static_cast<unsigned char>(value[0]);
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_'))
    {
        return false;
    }
    for (cyber::core::usize index = 1; index < value.Size(); ++index)
    {
        const auto ch = static_cast<unsigned char>(value[index]);
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              ch == '_'))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValidUri(StringView value) noexcept
{
    if (value.Data() == nullptr || value.Size() == 0 || value.Size() > kMaxMaterialUriBytes)
    {
        return false;
    }
    for (cyber::core::usize index = 0; index < value.Size(); ++index)
    {
        if (static_cast<unsigned char>(value[index]) < 0x20U ||
            static_cast<unsigned char>(value[index]) == 0x7FU)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsMaterialValueType(shaders::ValueType type) noexcept
{
    return type == shaders::ValueType::Float || type == shaders::ValueType::Float2 ||
           type == shaders::ValueType::Float3 || type == shaders::ValueType::Float4;
}

[[nodiscard]] u32 ComponentCount(shaders::ValueType type) noexcept
{
    switch (type)
    {
    case shaders::ValueType::Float:
        return 1;
    case shaders::ValueType::Float2:
        return 2;
    case shaders::ValueType::Float3:
        return 3;
    case shaders::ValueType::Float4:
        return 4;
    default:
        return 0;
    }
}

[[nodiscard]] bool IsFiniteValue(const MaterialValue& value) noexcept
{
    const u32 count = ComponentCount(value.type);
    if (count == 0)
    {
        return false;
    }
    for (u32 component = 0; component < count; ++component)
    {
        if (!std::isfinite(value.components[component]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsInRange(const MaterialValue& value, const MaterialValue& minimum,
                             const MaterialValue& maximum) noexcept
{
    if (value.type != minimum.type || value.type != maximum.type || !IsFiniteValue(value) ||
        !IsFiniteValue(minimum) || !IsFiniteValue(maximum))
    {
        return false;
    }
    const u32 count = ComponentCount(value.type);
    for (u32 component = 0; component < count; ++component)
    {
        if (minimum.components[component] > maximum.components[component] ||
            value.components[component] < minimum.components[component] ||
            value.components[component] > maximum.components[component])
        {
            return false;
        }
    }
    return true;
}

void CopyName(std::array<char, kMaxMaterialNameBytes + 1>& destination, StringView source) noexcept
{
    std::memcpy(destination.data(), source.Data(), source.Size());
    destination[source.Size()] = '\0';
}

[[nodiscard]] StringView NameView(const std::array<char, kMaxMaterialNameBytes + 1>& name) noexcept
{
    return StringView(name.data(), std::strlen(name.data()));
}

[[nodiscard]] const shaders::ConstantMember*
FindMaterialMember(Span<const shaders::Reflection* const> stages, StringView name,
                   u32& outBlockSize) noexcept
{
    const shaders::ConstantMember* found = nullptr;
    for (cyber::core::usize stage = 0; stage < stages.Size(); ++stage)
    {
        const shaders::Reflection* reflection = stages[stage];
        if (reflection == nullptr)
        {
            continue;
        }
        for (u32 resourceIndex = 0; resourceIndex < reflection->resourceCount; ++resourceIndex)
        {
            const shaders::ResourceBinding& resource = reflection->resources[resourceIndex];
            if (resource.kind != shaders::ResourceKind::ConstantBuffer ||
                resource.registerIndex != 1 || resource.registerSpace != 2)
            {
                continue;
            }
            outBlockSize = resource.sizeBytes;
            for (u32 memberIndex = 0; memberIndex < resource.memberCount; ++memberIndex)
            {
                const shaders::ConstantMember& member =
                    reflection->members[resource.firstMember + memberIndex];
                if (member.name.View() == name)
                {
                    if (found != nullptr &&
                        (found->type != member.type || found->offsetBytes != member.offsetBytes ||
                         found->sizeBytes != member.sizeBytes))
                    {
                        return nullptr;
                    }
                    found = &member;
                }
            }
        }
    }
    return found;
}

[[nodiscard]] u32 CountBindings(const ReflectedLayout& layout, rhi::BindingType type) noexcept
{
    u32 count = 0;
    const u32 slot = static_cast<u32>(rhi::BindGroupSlot::PerMaterial);
    for (u32 index = 0; index < layout.bindingCounts[slot]; ++index)
    {
        if (layout.slots[slot][index].entry.type == type)
        {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] bool ValidTextureSlotName(StringView name) noexcept
{
    return IsIdentifier(name);
}

[[nodiscard]] bool IsKnownColorSpace(MaterialColorSpace colorSpace) noexcept
{
    return colorSpace == MaterialColorSpace::Linear || colorSpace == MaterialColorSpace::Srgb;
}

[[nodiscard]] bool TextureColorSpaceMatches(StringView name, MaterialColorSpace colorSpace) noexcept
{
    const bool colorTexture =
        name == StringView("baseColorTexture") || name == StringView("emissiveTexture");
    return colorTexture ? colorSpace == MaterialColorSpace::Srgb
                        : colorSpace == MaterialColorSpace::Linear;
}

[[nodiscard]] bool GltfParameterMatches(StringView name, const MaterialValue& defaultValue,
                                        const MaterialValue& minimum,
                                        const MaterialValue& maximum) noexcept
{
    shaders::ValueType requiredType = static_cast<shaders::ValueType>(255);
    if (name == StringView("baseColorFactor"))
    {
        requiredType = shaders::ValueType::Float4;
    }
    else if (name == StringView("metallicFactor") || name == StringView("roughnessFactor") ||
             name == StringView("alphaCutoff") || name == StringView("occlusionStrength"))
    {
        requiredType = shaders::ValueType::Float;
    }
    else if (name == StringView("emissiveFactor"))
    {
        requiredType = shaders::ValueType::Float3;
    }
    else
    {
        return true;
    }
    if (defaultValue.type != requiredType || minimum.type != requiredType ||
        maximum.type != requiredType)
    {
        return false;
    }
    const u32 count = ComponentCount(requiredType);
    for (u32 component = 0; component < count; ++component)
    {
        const float expectedDefault = name == StringView("emissiveFactor") ? 0.0F
                                      : name == StringView("alphaCutoff")  ? 0.5F
                                                                           : 1.0F;
        const bool maximumMatches =
            name == StringView("alphaCutoff") || maximum.components[component] == 1.0F;
        if (defaultValue.components[component] != expectedDefault ||
            minimum.components[component] != 0.0F || !maximumMatches)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] core::Status<Error> InvalidParameter() noexcept
{
    return core::Status<Error>::Err(
        MaterialError(ErrorCode::InvalidArgument, "material: invalid parameter value"));
}

}  // namespace

Result<MaterialDefinition, Error>
MaterialDefinition::Create(const MaterialDefinitionDesc& desc) noexcept
{
    if (!IsIdentifier(desc.name) || desc.pipeline == nullptr || !desc.pipeline->HasBuilt() ||
        !desc.pipeline->Current().IsValid() || desc.stages.Empty() ||
        (desc.parameters.Data() == nullptr && !desc.parameters.Empty()) ||
        (desc.textureSlots.Data() == nullptr && !desc.textureSlots.Empty()) ||
        (desc.stages.Data() == nullptr && !desc.stages.Empty()) ||
        desc.parameters.Size() > kMaxMaterialParameters || desc.stages.Size() > 3 ||
        desc.textureSlots.Size() > kMaxMaterialTextureSlots ||
        (desc.queue != MaterialQueue::Opaque && desc.queue != MaterialQueue::Masked &&
         desc.queue != MaterialQueue::Transparent) ||
        desc.model != MaterialModel::MetallicRoughness ||
        (desc.renderState.cull != MaterialCullMode::None &&
         desc.renderState.cull != MaterialCullMode::Back &&
         desc.renderState.cull != MaterialCullMode::Front) ||
        (desc.renderState.blend != MaterialBlendMode::Off &&
         desc.renderState.blend != MaterialBlendMode::Alpha))
    {
        return Result<MaterialDefinition, Error>::Err(
            MaterialError(ErrorCode::InvalidArgument, "material definition: invalid descriptor"));
    }
    for (cyber::core::usize index = 0; index < desc.stages.Size(); ++index)
    {
        if (desc.stages[index] == nullptr)
        {
            return Result<MaterialDefinition, Error>::Err(MaterialError(
                ErrorCode::InvalidArgument, "material definition: stage reflection is null"));
        }
    }

    auto reflected = BuildReflectedLayout(desc.stages);
    if (!reflected)
    {
        return Result<MaterialDefinition, Error>::Err(reflected.Error());
    }
    const ReflectedLayout& layout = reflected.Value();
    if (!(layout == desc.pipeline->Layout()) || layout.materialConstantsSizeBytes == 0 ||
        layout.materialConstantsBinding == kNoBinding ||
        !desc.pipeline->BindGroupLayout(rhi::BindGroupSlot::PerMaterial).IsValid())
    {
        return Result<MaterialDefinition, Error>::Err(
            MaterialError(ErrorCode::InvalidArgument,
                          "material definition: pipeline layout has no matching material block"));
    }
    if (CountBindings(layout, rhi::BindingType::SampledTexture) != desc.textureSlots.Size() ||
        CountBindings(layout, rhi::BindingType::UniformBuffer) != 1)
    {
        return Result<MaterialDefinition, Error>::Err(
            MaterialError(ErrorCode::InvalidArgument,
                          "material definition: texture slots and reflected resources differ"));
    }
    if (CountBindings(layout, rhi::BindingType::Sampler) != 0 && !desc.defaultSampler.IsValid())
    {
        return Result<MaterialDefinition, Error>::Err(
            MaterialError(ErrorCode::InvalidArgument,
                          "material definition: reflected samplers need a default sampler"));
    }

    MaterialDefinition result{};
    CopyName(result.m_name, desc.name);
    result.m_model = desc.model;
    result.m_queue = desc.queue;
    result.m_renderState = desc.renderState;
    result.m_pipeline = desc.pipeline;
    result.m_defaultSampler = desc.defaultSampler;
    result.m_layoutGeneration = desc.pipeline->LayoutGeneration();
    result.m_parameterBlockSizeBytes = layout.materialConstantsSizeBytes;
    result.m_parameterBlockBinding = layout.materialConstantsBinding;

    u32 reflectedMemberCount = 0;
    for (cyber::core::usize stageIndex = 0; stageIndex < desc.stages.Size(); ++stageIndex)
    {
        const shaders::Reflection& reflection = *desc.stages[stageIndex];
        for (u32 resourceIndex = 0; resourceIndex < reflection.resourceCount; ++resourceIndex)
        {
            const shaders::ResourceBinding& resource = reflection.resources[resourceIndex];
            if (resource.kind == shaders::ResourceKind::ConstantBuffer &&
                resource.registerIndex == 1 && resource.registerSpace == 2)
            {
                reflectedMemberCount = resource.memberCount;
                break;
            }
        }
        if (reflectedMemberCount != 0)
        {
            break;
        }
    }
    if (reflectedMemberCount != desc.parameters.Size())
    {
        return Result<MaterialDefinition, Error>::Err(MaterialError(
            ErrorCode::InvalidArgument,
            "material definition: parameter schema does not cover reflected members"));
    }

    for (cyber::core::usize index = 0; index < desc.parameters.Size(); ++index)
    {
        const MaterialParameterDesc& source = desc.parameters[index];
        if (!IsIdentifier(source.name) || !IsMaterialValueType(source.defaultValue.type) ||
            source.defaultValue.type != source.minimum.type ||
            source.defaultValue.type != source.maximum.type ||
            !IsInRange(source.defaultValue, source.minimum, source.maximum) ||
            !GltfParameterMatches(source.name, source.defaultValue, source.minimum, source.maximum))
        {
            return Result<MaterialDefinition, Error>::Err(MaterialError(
                ErrorCode::InvalidArgument, "material definition: invalid parameter schema"));
        }
        for (cyber::core::usize prior = 0; prior < index; ++prior)
        {
            if (desc.parameters[prior].name == source.name)
            {
                return Result<MaterialDefinition, Error>::Err(MaterialError(
                    ErrorCode::InvalidArgument, "material definition: duplicate parameter name"));
            }
        }

        u32 blockSize = 0;
        const shaders::ConstantMember* member =
            FindMaterialMember(desc.stages, source.name, blockSize);
        if (member == nullptr || member->type != source.defaultValue.type ||
            member->offsetBytes > result.m_parameterBlockSizeBytes ||
            shaders::ValueTypeSize(member->type) >
                result.m_parameterBlockSizeBytes - member->offsetBytes)
        {
            return Result<MaterialDefinition, Error>::Err(
                MaterialError(ErrorCode::InvalidArgument,
                              "material definition: parameter does not match shader reflection"));
        }
        MaterialParameterSchema& target = result.m_parameters[index];
        CopyName(target.name, source.name);
        target.type = source.defaultValue.type;
        target.defaultValue = source.defaultValue;
        target.minimum = source.minimum;
        target.maximum = source.maximum;
        target.offsetBytes = member->offsetBytes;
    }

    for (cyber::core::usize index = 0; index < desc.textureSlots.Size(); ++index)
    {
        const MaterialTextureSlotDesc& source = desc.textureSlots[index];
        if (!ValidTextureSlotName(source.name) || !IsKnownColorSpace(source.colorSpace) ||
            !TextureColorSpaceMatches(source.name, source.colorSpace) ||
            (!source.defaultUri.Empty() && !IsValidUri(source.defaultUri)))
        {
            return Result<MaterialDefinition, Error>::Err(MaterialError(
                ErrorCode::InvalidArgument, "material definition: invalid texture-slot schema"));
        }
        for (cyber::core::usize prior = 0; prior < index; ++prior)
        {
            if (desc.textureSlots[prior].name == source.name)
            {
                return Result<MaterialDefinition, Error>::Err(
                    MaterialError(ErrorCode::InvalidArgument,
                                  "material definition: duplicate texture-slot name"));
            }
        }
        if (layout.FindBinding(rhi::BindGroupSlot::PerMaterial, source.name) == kNoBinding)
        {
            return Result<MaterialDefinition, Error>::Err(
                MaterialError(ErrorCode::InvalidArgument,
                              "material definition: texture slot is absent from reflection"));
        }
        MaterialTextureSchema& target = result.m_textureSlots[index];
        CopyName(target.name, source.name);
        target.colorSpace = source.colorSpace;
        target.defaultUriBytes = static_cast<u32>(source.defaultUri.Size());
        if (target.defaultUriBytes != 0)
        {
            std::memcpy(target.defaultUri.data(), source.defaultUri.Data(), target.defaultUriBytes);
            target.defaultUri[target.defaultUriBytes] = '\0';
        }
    }
    result.m_parameterCount = static_cast<u32>(desc.parameters.Size());
    result.m_textureSlotCount = static_cast<u32>(desc.textureSlots.Size());
    return Result<MaterialDefinition, Error>::Ok(result);
}

StringView MaterialDefinition::Name() const noexcept
{
    return NameView(m_name);
}

MaterialModel MaterialDefinition::Model() const noexcept
{
    return m_model;
}

MaterialQueue MaterialDefinition::Queue() const noexcept
{
    return m_queue;
}

MaterialRenderState MaterialDefinition::RenderState() const noexcept
{
    return m_renderState;
}

const HotGraphicsPipeline* MaterialDefinition::Pipeline() const noexcept
{
    return m_pipeline;
}

u32 MaterialDefinition::PipelineLayoutGeneration() const noexcept
{
    return m_layoutGeneration;
}

u32 MaterialDefinition::ParameterCount() const noexcept
{
    return m_parameterCount;
}

const MaterialParameterSchema* MaterialDefinition::Parameter(u32 index) const noexcept
{
    return index < m_parameterCount ? &m_parameters[index] : nullptr;
}

u32 MaterialDefinition::TextureSlotCount() const noexcept
{
    return m_textureSlotCount;
}

const MaterialTextureSchema* MaterialDefinition::TextureSlot(u32 index) const noexcept
{
    return index < m_textureSlotCount ? &m_textureSlots[index] : nullptr;
}

u32 MaterialDefinition::ParameterBlockSizeBytes() const noexcept
{
    return m_parameterBlockSizeBytes;
}

u32 MaterialDefinition::ParameterBlockBinding() const noexcept
{
    return m_parameterBlockBinding;
}

MaterialInstance::~MaterialInstance()
{
    Reset();
}

MaterialInstance::MaterialInstance(MaterialInstance&& other) noexcept
{
    *this = std::move(other);
}

MaterialInstance& MaterialInstance::operator=(MaterialInstance&& other) noexcept
{
    if (this != &other)
    {
        Reset();
        m_definition = other.m_definition;
        m_device = other.m_device;
        m_allocator = other.m_allocator;
        m_parameterBytes = other.m_parameterBytes;
        m_parameterBlockSizeBytes = other.m_parameterBlockSizeBytes;
        m_allocationSizeBytes = other.m_allocationSizeBytes;
        m_revision = other.m_revision;
        m_dirty = other.m_dirty;
        m_hasRecordedUpload = other.m_hasRecordedUpload;
        m_values = other.m_values;
        m_uniformBuffer = other.m_uniformBuffer;
        m_stagingBuffer = other.m_stagingBuffer;
        m_bindGroup = other.m_bindGroup;
        m_textureViews = other.m_textureViews;
        other.m_definition = nullptr;
        other.m_device = nullptr;
        other.m_allocator = nullptr;
        other.m_parameterBytes = nullptr;
        other.m_parameterBlockSizeBytes = 0;
        other.m_allocationSizeBytes = 0;
        other.m_revision = 0;
        other.m_dirty = false;
        other.m_hasRecordedUpload = false;
        other.m_values = {};
        other.m_uniformBuffer = {};
        other.m_stagingBuffer = {};
        other.m_bindGroup = {};
        other.m_textureViews = {};
    }
    return *this;
}

Result<MaterialInstance, Error>
MaterialInstance::Create(const MaterialDefinition& definition, rhi::Device& device,
                         memory::Allocator& allocator,
                         Span<const MaterialTextureView> textures) noexcept
{
    if (definition.m_pipeline == nullptr ||
        definition.m_pipeline->LayoutGeneration() != definition.m_layoutGeneration ||
        !definition.m_pipeline->Current().IsValid() ||
        (textures.Data() == nullptr && !textures.Empty()) ||
        textures.Size() != definition.m_textureSlotCount)
    {
        return Result<MaterialInstance, Error>::Err(
            MaterialError(ErrorCode::InvalidArgument, "material instance: stale definition or "
                                                      "incorrect texture count"));
    }
    const u32 alignment = device.Capabilities().limits.minUniformBufferOffsetAlignment;
    if (alignment == 0 || definition.m_parameterBlockSizeBytes == 0 ||
        definition.m_parameterBlockSizeBytes > std::numeric_limits<u32>::max() - (alignment - 1))
    {
        return Result<MaterialInstance, Error>::Err(
            MaterialError(ErrorCode::OutOfRange, "material instance: invalid uniform alignment"));
    }
    const u32 allocationBytes =
        ((definition.m_parameterBlockSizeBytes + alignment - 1) / alignment) * alignment;
    if (allocationBytes > device.Capabilities().limits.maxBufferBytes)
    {
        return Result<MaterialInstance, Error>::Err(
            MaterialError(ErrorCode::OutOfRange, "material instance: parameter block exceeds "
                                                 "device buffer limit"));
    }

    MaterialInstance instance{};
    instance.m_definition = &definition;
    instance.m_device = &device;
    instance.m_allocator = &allocator;
    instance.m_parameterBlockSizeBytes = definition.m_parameterBlockSizeBytes;
    instance.m_allocationSizeBytes = allocationBytes;
    instance.m_parameterBytes =
        static_cast<memory::Byte*>(allocator.Allocate(allocationBytes, alignof(std::max_align_t)));
    if (instance.m_parameterBytes == nullptr)
    {
        return Result<MaterialInstance, Error>::Err(
            MaterialError(ErrorCode::Internal, "material instance: parameter allocation failed"));
    }
    std::memset(instance.m_parameterBytes, 0, allocationBytes);

    for (u32 index = 0; index < definition.m_parameterCount; ++index)
    {
        const MaterialParameterSchema& schema = definition.m_parameters[index];
        instance.m_values[index] = schema.defaultValue;
        const u32 bytes = shaders::ValueTypeSize(schema.type);
        std::memcpy(instance.m_parameterBytes + schema.offsetBytes,
                    schema.defaultValue.components.data(), bytes);
    }
    for (u32 index = 0; index < definition.m_textureSlotCount; ++index)
    {
        const MaterialTextureSchema& schema = definition.m_textureSlots[index];
        const MaterialTextureView* match = nullptr;
        for (cyber::core::usize input = 0; input < textures.Size(); ++input)
        {
            if (textures[input].slot == NameView(schema.name))
            {
                if (match != nullptr)
                {
                    return Result<MaterialInstance, Error>::Err(MaterialError(
                        ErrorCode::InvalidArgument, "material instance: duplicate texture slot"));
                }
                match = &textures[input];
            }
        }
        if (match == nullptr || !match->view.IsValid())
        {
            return Result<MaterialInstance, Error>::Err(MaterialError(
                ErrorCode::InvalidArgument, "material instance: unresolved texture slot"));
        }
        instance.m_textureViews[index] = match->view;
    }

    rhi::BufferDesc uniformDesc{};
    uniformDesc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::CopyDestination;
    uniformDesc.sizeBytes = allocationBytes;
    uniformDesc.label = "material parameter block";
    auto uniform = device.CreateBuffer(uniformDesc);
    if (!uniform)
    {
        return Result<MaterialInstance, Error>::Err(uniform.Error());
    }
    instance.m_uniformBuffer = uniform.Value();

    rhi::BufferDesc stagingDesc{};
    stagingDesc.usage = rhi::BufferUsage::HostVisible | rhi::BufferUsage::CopySource;
    stagingDesc.sizeBytes = allocationBytes;
    stagingDesc.label = "material parameter staging";
    auto staging = device.CreateBuffer(stagingDesc);
    if (!staging)
    {
        return Result<MaterialInstance, Error>::Err(staging.Error());
    }
    instance.m_stagingBuffer = staging.Value();
    instance.m_dirty = true;
    instance.m_revision = 1;

    auto bindGroup = instance.RebuildBindGroup();
    if (!bindGroup)
    {
        return Result<MaterialInstance, Error>::Err(bindGroup.Error());
    }
    return Result<MaterialInstance, Error>::Ok(std::move(instance));
}

core::Status<Error> MaterialInstance::SetParameter(StringView name,
                                                   const MaterialValue& value) noexcept
{
    if (m_definition == nullptr || (name.Data() == nullptr && !name.Empty()) ||
        m_definition->m_pipeline == nullptr ||
        m_definition->m_pipeline->LayoutGeneration() != m_definition->m_layoutGeneration ||
        !IsMaterialValueType(value.type) || !IsFiniteValue(value))
    {
        return InvalidParameter();
    }
    for (u32 index = 0; index < m_definition->m_parameterCount; ++index)
    {
        const MaterialParameterSchema& schema = m_definition->m_parameters[index];
        if (NameView(schema.name) != name)
        {
            continue;
        }
        if (value.type != schema.type || !IsInRange(value, schema.minimum, schema.maximum))
        {
            return InvalidParameter();
        }
        if (m_values[index].components == value.components)
        {
            return core::Status<Error>::Ok();
        }
        if (m_revision == std::numeric_limits<u64>::max())
        {
            return core::Status<Error>::Err(
                MaterialError(ErrorCode::OutOfRange, "material instance: revision exhausted"));
        }
        m_values[index] = value;
        std::memcpy(m_parameterBytes + schema.offsetBytes, value.components.data(),
                    shaders::ValueTypeSize(schema.type));
        ++m_revision;
        m_dirty = true;
        return core::Status<Error>::Ok();
    }
    return core::Status<Error>::Err(
        MaterialError(ErrorCode::NotFound, "material instance: parameter name not found"));
}

core::Status<Error> MaterialInstance::SetTexture(StringView slot,
                                                 rhi::TextureViewHandle view) noexcept
{
    if (m_definition == nullptr || m_device == nullptr ||
        (slot.Data() == nullptr && !slot.Empty()) || !view.IsValid() ||
        m_definition->m_pipeline == nullptr ||
        m_definition->m_pipeline->LayoutGeneration() != m_definition->m_layoutGeneration)
    {
        return core::Status<Error>::Err(
            MaterialError(ErrorCode::InvalidArgument, "material instance: invalid texture update"));
    }
    if (m_revision == std::numeric_limits<u64>::max())
    {
        return core::Status<Error>::Err(
            MaterialError(ErrorCode::OutOfRange, "material instance: revision exhausted"));
    }
    for (u32 index = 0; index < m_definition->m_textureSlotCount; ++index)
    {
        if (NameView(m_definition->m_textureSlots[index].name) != slot)
        {
            continue;
        }
        const rhi::TextureViewHandle previous = m_textureViews[index];
        m_textureViews[index] = view;
        auto rebuilt = RebuildBindGroup();
        if (!rebuilt)
        {
            m_textureViews[index] = previous;
            return rebuilt;
        }
        ++m_revision;
        return core::Status<Error>::Ok();
    }
    return core::Status<Error>::Err(
        MaterialError(ErrorCode::NotFound, "material instance: texture slot not found"));
}

core::Status<Error> MaterialInstance::RecordUpload(rhi::CommandList& commandList) noexcept
{
    if (m_device == nullptr || !m_uniformBuffer.IsValid() || !m_stagingBuffer.IsValid() ||
        m_definition == nullptr || m_definition->m_pipeline == nullptr ||
        m_definition->m_pipeline->LayoutGeneration() != m_definition->m_layoutGeneration ||
        commandList.Queue() != rhi::QueueKind::Graphics)
    {
        return core::Status<Error>::Err(MaterialError(
            ErrorCode::InvalidArgument,
            "material instance: requires live resources and a graphics command list"));
    }
    if (!m_dirty)
    {
        return core::Status<Error>::Ok();
    }

    auto written = m_device->WriteBuffer(
        m_stagingBuffer, 0, Span<const memory::Byte>(m_parameterBytes, m_allocationSizeBytes));
    if (!written)
    {
        return written;
    }

    rhi::BufferBarrier initial[2]{};
    u32 initialCount = 0;
    if (!m_hasRecordedUpload)
    {
        initial[initialCount++] = {m_stagingBuffer, rhi::ResourceState::Undefined,
                                   rhi::ResourceState::CopySource};
        initial[initialCount++] = {m_uniformBuffer, rhi::ResourceState::Undefined,
                                   rhi::ResourceState::CopyDestination};
    }
    else
    {
        initial[initialCount++] = {m_uniformBuffer, rhi::ResourceState::ConstantBuffer,
                                   rhi::ResourceState::CopyDestination};
    }
    auto barrier = commandList.Barrier(Span<const rhi::BufferBarrier>(initial, initialCount));
    if (!barrier)
    {
        return barrier;
    }
    const rhi::BufferCopy copy{m_stagingBuffer, 0, m_uniformBuffer, 0, m_allocationSizeBytes};
    auto copied = commandList.CopyBuffer(copy);
    if (!copied)
    {
        return copied;
    }
    const rhi::BufferBarrier final{m_uniformBuffer, rhi::ResourceState::CopyDestination,
                                   rhi::ResourceState::ConstantBuffer};
    auto transitioned = commandList.Barrier(Span<const rhi::BufferBarrier>(&final, 1));
    if (!transitioned)
    {
        return transitioned;
    }
    m_hasRecordedUpload = true;
    m_dirty = false;
    return core::Status<Error>::Ok();
}

rhi::BindGroupHandle MaterialInstance::BindGroup() const noexcept
{
    return m_definition != nullptr && m_definition->m_pipeline != nullptr &&
                   m_definition->m_pipeline->LayoutGeneration() == m_definition->m_layoutGeneration
               ? m_bindGroup
               : rhi::BindGroupHandle{};
}

u64 MaterialInstance::Revision() const noexcept
{
    return m_revision;
}

bool MaterialInstance::IsDirty() const noexcept
{
    return m_dirty;
}

u32 MaterialInstance::ParameterBlockSizeBytes() const noexcept
{
    return m_parameterBlockSizeBytes;
}

Result<MaterialValue, Error> MaterialInstance::GetParameter(StringView name) const noexcept
{
    if (m_definition == nullptr || (name.Data() == nullptr && !name.Empty()) ||
        m_definition->m_pipeline == nullptr ||
        m_definition->m_pipeline->LayoutGeneration() != m_definition->m_layoutGeneration)
    {
        return Result<MaterialValue, Error>::Err(
            MaterialError(ErrorCode::InvalidArgument, "material instance: no definition"));
    }
    for (u32 index = 0; index < m_definition->m_parameterCount; ++index)
    {
        if (NameView(m_definition->m_parameters[index].name) == name)
        {
            return Result<MaterialValue, Error>::Ok(m_values[index]);
        }
    }
    return Result<MaterialValue, Error>::Err(
        MaterialError(ErrorCode::NotFound, "material instance: parameter name not found"));
}

void MaterialInstance::Reset() noexcept
{
    if (m_device != nullptr)
    {
        if (m_bindGroup.IsValid())
        {
            static_cast<void>(m_device->DestroyBindGroup(m_bindGroup));
        }
        if (m_stagingBuffer.IsValid())
        {
            static_cast<void>(m_device->DestroyBuffer(m_stagingBuffer));
        }
        if (m_uniformBuffer.IsValid())
        {
            static_cast<void>(m_device->DestroyBuffer(m_uniformBuffer));
        }
    }
    if (m_allocator != nullptr && m_parameterBytes != nullptr)
    {
        m_allocator->Deallocate(m_parameterBytes);
    }
    m_definition = nullptr;
    m_device = nullptr;
    m_allocator = nullptr;
    m_parameterBytes = nullptr;
    m_parameterBlockSizeBytes = 0;
    m_allocationSizeBytes = 0;
    m_revision = 0;
    m_dirty = false;
    m_hasRecordedUpload = false;
    m_values = {};
    m_uniformBuffer = {};
    m_stagingBuffer = {};
    m_bindGroup = {};
    m_textureViews = {};
}

core::Status<Error> MaterialInstance::RebuildBindGroup() noexcept
{
    if (m_device == nullptr || m_definition == nullptr ||
        m_definition->m_pipeline->LayoutGeneration() != m_definition->m_layoutGeneration)
    {
        return core::Status<Error>::Err(MaterialError(
            ErrorCode::InvalidArgument, "material instance: pipeline layout changed"));
    }

    const ReflectedLayout& layout = m_definition->m_pipeline->Layout();
    constexpr u32 kSlot = static_cast<u32>(rhi::BindGroupSlot::PerMaterial);
    std::array<rhi::BindingResource, kMaxReflectedBindingsPerSlot> resources{};
    for (u32 binding = 0; binding < layout.bindingCounts[kSlot]; ++binding)
    {
        const ReflectedBinding& reflected = layout.slots[kSlot][binding];
        rhi::BindingResource& resource = resources[binding];
        resource.type = reflected.entry.type;
        if (resource.type == rhi::BindingType::UniformBuffer)
        {
            if (binding != m_definition->m_parameterBlockBinding)
            {
                return core::Status<Error>::Err(
                    MaterialError(ErrorCode::Unsupported,
                                  "material instance: unexpected uniform buffer binding"));
            }
            resource.buffer = m_uniformBuffer;
            resource.size = m_allocationSizeBytes;
        }
        else if (resource.type == rhi::BindingType::SampledTexture)
        {
            u32 textureIndex = kMaxMaterialTextureSlots;
            for (u32 index = 0; index < m_definition->m_textureSlotCount; ++index)
            {
                if (NameView(m_definition->m_textureSlots[index].name) == reflected.name.View())
                {
                    textureIndex = index;
                    break;
                }
            }
            if (textureIndex == kMaxMaterialTextureSlots || !m_textureViews[textureIndex].IsValid())
            {
                return core::Status<Error>::Err(MaterialError(
                    ErrorCode::InvalidArgument, "material instance: unresolved reflected texture"));
            }
            resource.textureView = m_textureViews[textureIndex];
        }
        else if (resource.type == rhi::BindingType::Sampler)
        {
            resource.sampler = m_definition->m_defaultSampler;
        }
        else
        {
            return core::Status<Error>::Err(
                MaterialError(ErrorCode::Unsupported,
                              "material instance: unsupported per-material binding type"));
        }
    }

    rhi::BindGroupDesc desc{};
    desc.layout = m_definition->m_pipeline->BindGroupLayout(rhi::BindGroupSlot::PerMaterial);
    desc.resources =
        Span<const rhi::BindingResource>(resources.data(), layout.bindingCounts[kSlot]);
    desc.label = "material instance";
    auto created = m_device->CreateBindGroup(desc);
    if (!created)
    {
        return core::Status<Error>::Err(created.Error());
    }
    const rhi::BindGroupHandle previous = m_bindGroup;
    if (previous.IsValid())
    {
        auto destroyed = m_device->DestroyBindGroup(previous);
        if (!destroyed)
        {
            static_cast<void>(m_device->DestroyBindGroup(created.Value()));
            return destroyed;
        }
    }
    m_bindGroup = created.Value();
    return core::Status<Error>::Ok();
}

core::Status<Error> SortMaterialDraws(Span<MaterialDrawSortKey> draws) noexcept
{
    if (draws.Data() == nullptr && !draws.Empty())
    {
        return core::Status<Error>::Err(
            MaterialError(ErrorCode::InvalidArgument, "material sort: non-empty span is null"));
    }
    for (cyber::core::usize index = 0; index < draws.Size(); ++index)
    {
        const MaterialDrawSortKey& draw = draws[index];
        if (draw.queue >= MaterialQueue::Count || !std::isfinite(draw.viewDepth) ||
            draw.viewDepth < 0.0F)
        {
            return core::Status<Error>::Err(
                MaterialError(ErrorCode::InvalidArgument, "material sort: invalid queue or depth"));
        }
    }

    const auto less = [](const MaterialDrawSortKey& lhs,
                         const MaterialDrawSortKey& rhs) noexcept -> bool
    {
        if (lhs.queue != rhs.queue)
        {
            return lhs.queue < rhs.queue;
        }
        if (lhs.queue == MaterialQueue::Transparent)
        {
            if (lhs.viewDepth != rhs.viewDepth)
            {
                return lhs.viewDepth > rhs.viewDepth;
            }
        }
        else
        {
            if (lhs.pipelineIdentity != rhs.pipelineIdentity)
            {
                return lhs.pipelineIdentity < rhs.pipelineIdentity;
            }
            if (lhs.materialIdentity != rhs.materialIdentity)
            {
                return lhs.materialIdentity < rhs.materialIdentity;
            }
            if (lhs.viewDepth != rhs.viewDepth)
            {
                return lhs.viewDepth < rhs.viewDepth;
            }
        }
        if (lhs.pipelineIdentity != rhs.pipelineIdentity)
        {
            return lhs.pipelineIdentity < rhs.pipelineIdentity;
        }
        if (lhs.materialIdentity != rhs.materialIdentity)
        {
            return lhs.materialIdentity < rhs.materialIdentity;
        }
        return lhs.submissionSequence < rhs.submissionSequence;
    };

    // Stable insertion sort keeps equal sort keys in submission order without
    // allocating temporary storage; Phase 18 sorts small material batches.
    for (cyber::core::usize index = 1; index < draws.Size(); ++index)
    {
        const MaterialDrawSortKey value = draws[index];
        cyber::core::usize position = index;
        while (position > 0 && less(value, draws[position - 1]))
        {
            draws[position] = draws[position - 1];
            --position;
        }
        draws[position] = value;
    }
    return core::Status<Error>::Ok();
}

}  // namespace cyber::renderer
