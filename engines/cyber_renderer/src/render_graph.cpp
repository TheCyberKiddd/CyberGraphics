#include <cyber/core/log.h>
#include <cyber/renderer/render_graph.h>

#include <cstdio>
#include <cstring>

namespace cyber::renderer
{
namespace
{

using cyber::core::ErrorCode;
using cyber::memory::Span;
using cyber::rhi::BufferUsage;
using cyber::rhi::ResourceState;
using cyber::rhi::TextureUsage;

constexpr u32 kInvalidIndex = 0xFFFFFFFFU;
constexpr u32 kMaximumNameBytes = 63;

[[nodiscard]] Error GraphError(ErrorCode code, const char* message) noexcept
{
    return Error{code, message};
}

[[nodiscard]] bool CopyName(const char* name, char (&destination)[64]) noexcept
{
    if (name == nullptr || name[0] == '\0')
    {
        return false;
    }

    usize length = 0;
    while (length <= kMaximumNameBytes && name[length] != '\0')
    {
        ++length;
    }
    if (length == 0 || length > kMaximumNameBytes)
    {
        return false;
    }

    std::memcpy(destination, name, length);
    destination[length] = '\0';
    return true;
}

[[nodiscard]] bool SupportsState(BufferUsage usage, ResourceState state) noexcept
{
    if (!cyber::rhi::IsBufferResourceState(state) || !cyber::rhi::IsValidStateSet(state))
    {
        return false;
    }

    return (!cyber::rhi::HasState(state, ResourceState::CopySource) ||
            cyber::rhi::HasUsage(usage, BufferUsage::CopySource)) &&
           (!cyber::rhi::HasState(state, ResourceState::CopyDestination) ||
            cyber::rhi::HasUsage(usage, BufferUsage::CopyDestination)) &&
           (!cyber::rhi::HasState(state, ResourceState::VertexBuffer) ||
            cyber::rhi::HasUsage(usage, BufferUsage::Vertex)) &&
           (!cyber::rhi::HasState(state, ResourceState::IndexBuffer) ||
            cyber::rhi::HasUsage(usage, BufferUsage::Index)) &&
           (!cyber::rhi::HasState(state, ResourceState::ConstantBuffer) ||
            cyber::rhi::HasUsage(usage, BufferUsage::Uniform)) &&
           (!cyber::rhi::HasState(state, ResourceState::ShaderRead) ||
            cyber::rhi::HasUsage(usage, BufferUsage::Storage)) &&
           (!cyber::rhi::HasState(state, ResourceState::ShaderWrite) ||
            cyber::rhi::HasUsage(usage, BufferUsage::Storage)) &&
           (!cyber::rhi::HasState(state, ResourceState::IndirectArgument) ||
            cyber::rhi::HasUsage(usage, BufferUsage::Indirect));
}

[[nodiscard]] bool SupportsState(TextureUsage usage, ResourceState state) noexcept
{
    if (!cyber::rhi::IsTextureResourceState(state) || !cyber::rhi::IsValidStateSet(state))
    {
        return false;
    }

    return (!cyber::rhi::HasState(state, ResourceState::CopySource) ||
            cyber::rhi::HasUsage(usage, TextureUsage::CopySource)) &&
           (!cyber::rhi::HasState(state, ResourceState::CopyDestination) ||
            cyber::rhi::HasUsage(usage, TextureUsage::CopyDestination)) &&
           (!cyber::rhi::HasState(state, ResourceState::ShaderRead) ||
            cyber::rhi::HasUsage(usage, TextureUsage::Sampled)) &&
           (!cyber::rhi::HasState(state, ResourceState::ShaderWrite) ||
            cyber::rhi::HasUsage(usage, TextureUsage::Storage)) &&
           (!cyber::rhi::HasState(state, ResourceState::RenderTarget) ||
            cyber::rhi::HasUsage(usage, TextureUsage::RenderTarget)) &&
           (!cyber::rhi::HasState(state, ResourceState::DepthRead) ||
            cyber::rhi::HasUsage(usage, TextureUsage::DepthStencil)) &&
           (!cyber::rhi::HasState(state, ResourceState::DepthWrite) ||
            cyber::rhi::HasUsage(usage, TextureUsage::DepthStencil));
}

struct DumpWriter
{
    char* data = nullptr;
    usize capacity = 0;
    usize size = 0;
    bool overflow = false;

    void Append(const char* text) noexcept
    {
        const usize length = std::strlen(text);
        if (length > ~usize{0} - size)
        {
            size = ~usize{0};
            overflow = true;
            return;
        }
        if (data != nullptr)
        {
            if (size > capacity || length > capacity - size)
            {
                overflow = true;
            }
            else
            {
                std::memcpy(data + size, text, length);
            }
        }
        size += length;
    }

    void AppendNumber(u32 value) noexcept
    {
        char buffer[16]{};
        const int length =
            std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(value));
        if (length > 0 && static_cast<usize>(length) < sizeof(buffer))
        {
            Append(buffer);
        }
    }

    void AppendEscaped(const char* text) noexcept
    {
        for (usize index = 0; text[index] != '\0'; ++index)
        {
            const char character = text[index];
            if (character == '"' || character == '\\')
            {
                Append("\\");
            }
            if (character == '\n')
            {
                Append("\\n");
            }
            else if (character != '\r')
            {
                char value[2]{character, '\0'};
                Append(value);
            }
        }
    }
};

}  // namespace

RenderPassContext::RenderPassContext(const RenderGraph& graph, u32 passIndex,
                                     cyber::rhi::CommandList& commandList) noexcept
    : m_graph(&graph), m_passIndex(passIndex), m_commandList(&commandList)
{
}

cyber::rhi::CommandList& RenderPassContext::Commands() const noexcept
{
    return *m_commandList;
}

cyber::rhi::BufferHandle RenderPassContext::GetBuffer(GraphResourceHandle resource) const noexcept
{
    if (m_graph == nullptr || !m_graph->IsValidResource(resource) ||
        !m_graph->IsDeclaredByPass(m_passIndex, resource.index))
    {
        return {};
    }
    return m_graph->ResolveBuffer(resource.index);
}

cyber::rhi::TextureHandle RenderPassContext::GetTexture(GraphResourceHandle resource) const noexcept
{
    if (m_graph == nullptr || !m_graph->IsValidResource(resource) ||
        !m_graph->IsDeclaredByPass(m_passIndex, resource.index))
    {
        return {};
    }
    return m_graph->ResolveTexture(resource.index);
}

RenderPassBuilder::RenderPassBuilder(RenderGraph& graph, u32 passIndex) noexcept
    : m_graph(&graph), m_passIndex(passIndex)
{
}

Status<Error> RenderPassBuilder::Read(GraphResourceHandle resource, ResourceState state) noexcept
{
    if (m_graph == nullptr || !cyber::rhi::IsReadState(state))
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: read needs a valid read state"));
    }
    return m_graph->AddUse(m_passIndex, resource, state, false);
}

Status<Error> RenderPassBuilder::Write(GraphResourceHandle resource, ResourceState state) noexcept
{
    if (m_graph == nullptr || !cyber::rhi::IsWriteState(state) ||
        !cyber::rhi::IsValidStateSet(state))
    {
        return Status<Error>::Err(GraphError(ErrorCode::InvalidArgument,
                                             "render graph: write needs one valid write state"));
    }
    return m_graph->AddUse(m_passIndex, resource, state, true);
}

RenderGraph::RenderGraph(cyber::rhi::Device& device, cyber::memory::Allocator& allocator) noexcept
    : m_device(&device), m_allocator(&allocator), m_resources(allocator), m_passes(allocator),
      m_uses(allocator), m_dependencies(allocator), m_slots(allocator)
{
}

RenderGraph::~RenderGraph()
{
    const Status<Error> destroyed = Destroy();
    if (!destroyed)
    {
        CG_LOG(Error, Renderer, "render graph: failed to destroy transient resources: %s",
               destroyed.Error().message == nullptr ? "unknown error" : destroyed.Error().message);
    }
}

Result<GraphResourceHandle, Error>
RenderGraph::ImportBuffer(const char* name, cyber::rhi::BufferHandle buffer) noexcept
{
    if (m_device == nullptr || !m_device->IsValid() || m_compiled || m_destroyed || m_settingUp ||
        m_resources.Size() >= kInvalidIndex)
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: cannot import buffer now"));
    }

    ResourceRecord record{};
    if (!CopyName(name, record.name) || !buffer.IsValid())
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid buffer import"));
    }
    const cyber::rhi::BufferInfo info = m_device->BufferInfoOf(buffer);
    if (!info.valid)
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: imported buffer is stale"));
    }
    for (usize index = 0; index < m_resources.Size(); ++index)
    {
        if (m_resources[index].imported && m_resources[index].kind == ResourceKind::Buffer &&
            m_resources[index].importedBuffer == buffer)
        {
            return Result<GraphResourceHandle, Error>::Err(
                GraphError(ErrorCode::AlreadyExists, "render graph: buffer imported twice"));
        }
    }

    record.kind = ResourceKind::Buffer;
    record.imported = true;
    record.initialState = m_device->BufferStateOf(buffer);
    record.currentState = record.initialState;
    record.bufferDesc.usage = info.usage;
    record.bufferDesc.sizeBytes = info.sizeBytes;
    record.bufferDesc.elementStride = info.elementStride;
    record.bufferDesc.lifetime = info.lifetime;
    record.bufferDesc.label = record.name;
    record.importedBuffer = buffer;
    if (record.initialState != ResourceState::Undefined &&
        !SupportsState(info.usage, record.initialState))
    {
        return Result<GraphResourceHandle, Error>::Err(GraphError(
            ErrorCode::InvalidArgument, "render graph: imported buffer state is invalid"));
    }
    if (!m_resources.TryPushBack(record))
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: resource metadata allocation failed"));
    }
    return Result<GraphResourceHandle, Error>::Ok(
        GraphResourceHandle{static_cast<u32>(m_resources.Size() - 1), m_generation, this});
}

Result<GraphResourceHandle, Error>
RenderGraph::ImportTexture(const char* name, cyber::rhi::TextureHandle texture) noexcept
{
    if (m_device == nullptr || !m_device->IsValid() || m_compiled || m_destroyed || m_settingUp ||
        m_resources.Size() >= kInvalidIndex)
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: cannot import texture now"));
    }

    ResourceRecord record{};
    if (!CopyName(name, record.name) || !texture.IsValid())
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid texture import"));
    }
    const cyber::rhi::TextureInfo info = m_device->TextureInfoOf(texture);
    if (!info.valid)
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: imported texture is stale"));
    }
    for (usize index = 0; index < m_resources.Size(); ++index)
    {
        if (m_resources[index].imported && m_resources[index].kind == ResourceKind::Texture &&
            m_resources[index].importedTexture == texture)
        {
            return Result<GraphResourceHandle, Error>::Err(
                GraphError(ErrorCode::AlreadyExists, "render graph: texture imported twice"));
        }
    }

    record.kind = ResourceKind::Texture;
    record.imported = true;
    record.textureDesc.dimension = info.dimension;
    record.textureDesc.format = info.format;
    record.textureDesc.usage = info.usage;
    record.textureDesc.width = info.width;
    record.textureDesc.height = info.height;
    record.textureDesc.depth = info.depth;
    record.textureDesc.arrayLayers = info.arrayLayers;
    record.textureDesc.mipLevels = info.mipLevels;
    record.textureDesc.sampleCount = info.sampleCount;
    record.textureDesc.lifetime = info.lifetime;
    record.textureDesc.label = record.name;
    record.importedTexture = texture;

    bool firstState = true;
    for (u32 mip = 0; mip < info.mipLevels; ++mip)
    {
        for (u32 layer = 0; layer < info.arrayLayers; ++layer)
        {
            const ResourceState state = m_device->TextureStateOf(texture, mip, layer);
            if (firstState)
            {
                record.initialState = state;
                record.currentState = state;
                firstState = false;
            }
            else if (record.initialState != state)
            {
                return Result<GraphResourceHandle, Error>::Err(
                    GraphError(ErrorCode::InvalidArgument,
                               "render graph: imported texture subresources have mixed states"));
            }
        }
    }
    if (record.initialState != ResourceState::Undefined &&
        !SupportsState(info.usage, record.initialState))
    {
        return Result<GraphResourceHandle, Error>::Err(GraphError(
            ErrorCode::InvalidArgument, "render graph: imported texture state is invalid"));
    }
    if (!m_resources.TryPushBack(record))
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: resource metadata allocation failed"));
    }
    return Result<GraphResourceHandle, Error>::Ok(
        GraphResourceHandle{static_cast<u32>(m_resources.Size() - 1), m_generation, this});
}

Result<GraphResourceHandle, Error>
RenderGraph::CreateTransientBuffer(const char* name, const cyber::rhi::BufferDesc& desc) noexcept
{
    if (m_device == nullptr || !m_device->IsValid() || m_compiled || m_destroyed || m_settingUp ||
        m_resources.Size() >= kInvalidIndex || desc.usage == BufferUsage::None ||
        desc.sizeBytes == 0)
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid transient buffer"));
    }

    ResourceRecord record{};
    if (!CopyName(name, record.name))
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid resource name"));
    }
    record.kind = ResourceKind::Buffer;
    record.bufferDesc = desc;
    record.bufferDesc.lifetime = cyber::rhi::MemoryLifetime::Transient;
    record.bufferDesc.label = record.name;
    if (!m_resources.TryPushBack(record))
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: resource metadata allocation failed"));
    }
    return Result<GraphResourceHandle, Error>::Ok(
        GraphResourceHandle{static_cast<u32>(m_resources.Size() - 1), m_generation, this});
}

Result<GraphResourceHandle, Error>
RenderGraph::CreateTransientTexture(const char* name, const cyber::rhi::TextureDesc& desc) noexcept
{
    if (m_device == nullptr || !m_device->IsValid() || m_compiled || m_destroyed || m_settingUp ||
        m_resources.Size() >= kInvalidIndex || desc.usage == TextureUsage::None || desc.width == 0)
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid transient texture"));
    }

    ResourceRecord record{};
    if (!CopyName(name, record.name))
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid resource name"));
    }
    record.kind = ResourceKind::Texture;
    record.textureDesc = desc;
    record.textureDesc.lifetime = cyber::rhi::MemoryLifetime::Transient;
    record.textureDesc.label = record.name;
    if (!m_resources.TryPushBack(record))
    {
        return Result<GraphResourceHandle, Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: resource metadata allocation failed"));
    }
    return Result<GraphResourceHandle, Error>::Ok(
        GraphResourceHandle{static_cast<u32>(m_resources.Size() - 1), m_generation, this});
}

Status<Error> RenderGraph::AddPass(const RenderPassDesc& desc) noexcept
{
    if (m_device == nullptr || !m_device->IsValid() || m_compiled || m_destroyed || m_settingUp ||
        desc.setup == nullptr || desc.execute == nullptr || m_passes.Size() >= kInvalidIndex)
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid pass declaration"));
    }

    PassRecord pass{};
    if (!CopyName(desc.name, pass.name))
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid pass name"));
    }
    pass.useStart = static_cast<u32>(m_uses.Size());
    pass.sideEffect = desc.sideEffect;
    pass.execute = desc.execute;
    pass.userData = desc.userData;
    if (!m_passes.TryPushBack(pass))
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: pass metadata allocation failed"));
    }

    const u32 passIndex = static_cast<u32>(m_passes.Size() - 1);
    RenderPassBuilder builder(*this, passIndex);
    m_settingUp = true;
    m_setupPass = passIndex;
    const Status<Error> setup = desc.setup(builder, desc.userData);
    m_settingUp = false;
    m_setupPass = kInvalidIndex;
    if (!setup)
    {
        (void)m_uses.TryResize(pass.useStart);
        m_passes.PopBack();
        return setup;
    }
    m_passes[passIndex].useCount = static_cast<u32>(m_uses.Size()) - pass.useStart;
    return Status<Error>::Ok();
}

Status<Error> RenderGraph::Export(GraphResourceHandle resource, ResourceState finalState) noexcept
{
    if (!IsValidResource(resource) || m_compiled || m_destroyed || m_settingUp)
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid export"));
    }
    ResourceRecord& record = m_resources[resource.index];
    if (record.exported || (finalState != ResourceState::Undefined &&
                            (record.kind == ResourceKind::Buffer
                                 ? !SupportsState(record.bufferDesc.usage, finalState)
                                 : !SupportsState(record.textureDesc.usage, finalState))))
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: duplicate or invalid export"));
    }
    record.exported = true;
    record.finalState = finalState;
    return Status<Error>::Ok();
}

Status<Error> RenderGraph::Compile() noexcept
{
    if (m_device == nullptr || !m_device->IsValid() || m_compiled || m_destroyed || m_settingUp)
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: cannot compile now"));
    }

    m_dependencies.Clear();
    m_slots.Clear();
    for (usize resourceIndex = 0; resourceIndex < m_resources.Size(); ++resourceIndex)
    {
        m_resources[resourceIndex].hasLiveUse = false;
        m_resources[resourceIndex].firstPass = kInvalidIndex;
        m_resources[resourceIndex].lastPass = 0;
        m_resources[resourceIndex].physicalSlot = kInvalidIndex;
    }
    for (usize passIndex = 0; passIndex < m_passes.Size(); ++passIndex)
    {
        m_passes[passIndex].live = false;
    }

    cyber::memory::DynamicArray<bool> contentsDefined(*m_allocator);
    if (!contentsDefined.TryResize(m_resources.Size()))
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: content-state allocation failed"));
    }
    for (usize resourceIndex = 0; resourceIndex < m_resources.Size(); ++resourceIndex)
    {
        const ResourceRecord& resource = m_resources[resourceIndex];
        contentsDefined[resourceIndex] =
            resource.imported && resource.initialState != ResourceState::Undefined;
    }
    for (usize passIndex = 0; passIndex < m_passes.Size(); ++passIndex)
    {
        const PassRecord& pass = m_passes[passIndex];
        for (u32 useIndex = 0; useIndex < pass.useCount; ++useIndex)
        {
            const UseRecord& use = m_uses[pass.useStart + useIndex];
            if (!use.write && !contentsDefined[use.resourceIndex])
            {
                return Status<Error>::Err(
                    GraphError(ErrorCode::InvalidArgument,
                               "render graph: transient resource is read before its first write"));
            }
            if (use.write)
            {
                contentsDefined[use.resourceIndex] = true;
            }

            for (usize priorPass = passIndex; priorPass > 0; --priorPass)
            {
                const u32 candidateIndex = static_cast<u32>(priorPass - 1);
                const PassRecord& candidate = m_passes[candidateIndex];
                bool foundAccess = false;
                bool foundWrite = false;
                for (u32 candidateUseIndex = 0; candidateUseIndex < candidate.useCount;
                     ++candidateUseIndex)
                {
                    const UseRecord& candidateUse = m_uses[candidate.useStart + candidateUseIndex];
                    if (candidateUse.resourceIndex == use.resourceIndex)
                    {
                        foundAccess = true;
                        foundWrite = candidateUse.write;
                        break;
                    }
                }
                if (!foundAccess)
                {
                    continue;
                }
                if (use.write || foundWrite)
                {
                    bool duplicate = false;
                    for (usize dependencyIndex = 0; dependencyIndex < m_dependencies.Size();
                         ++dependencyIndex)
                    {
                        const Dependency& dependency = m_dependencies[dependencyIndex];
                        if (dependency.producer == candidateIndex &&
                            dependency.consumer == passIndex)
                        {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate && !m_dependencies.TryPushBack(
                                          Dependency{candidateIndex, static_cast<u32>(passIndex)}))
                    {
                        return Status<Error>::Err(GraphError(
                            ErrorCode::Internal, "render graph: dependency allocation failed"));
                    }
                }
                if (foundWrite)
                {
                    break;
                }
            }
        }
    }

    for (usize passIndex = 0; passIndex < m_passes.Size(); ++passIndex)
    {
        if (m_passes[passIndex].sideEffect)
        {
            m_passes[passIndex].live = true;
        }
    }
    for (usize resourceIndex = 0; resourceIndex < m_resources.Size(); ++resourceIndex)
    {
        ResourceRecord& resource = m_resources[resourceIndex];
        if (!resource.exported)
        {
            continue;
        }
        bool foundWriter = false;
        for (usize passIndex = m_passes.Size(); passIndex > 0 && !foundWriter; --passIndex)
        {
            const PassRecord& pass = m_passes[passIndex - 1];
            for (u32 useIndex = 0; useIndex < pass.useCount; ++useIndex)
            {
                const UseRecord& use = m_uses[pass.useStart + useIndex];
                if (use.resourceIndex == resourceIndex && use.write)
                {
                    m_passes[passIndex - 1].live = true;
                    foundWriter = true;
                    break;
                }
            }
        }
        if (!foundWriter && !resource.imported)
        {
            return Status<Error>::Err(GraphError(ErrorCode::InvalidArgument,
                                                 "render graph: exported transient has no writer"));
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (usize dependencyIndex = 0; dependencyIndex < m_dependencies.Size(); ++dependencyIndex)
        {
            const Dependency& dependency = m_dependencies[dependencyIndex];
            if (m_passes[dependency.consumer].live && !m_passes[dependency.producer].live)
            {
                m_passes[dependency.producer].live = true;
                changed = true;
            }
        }
    }

    for (usize passIndex = 0; passIndex < m_passes.Size(); ++passIndex)
    {
        if (!m_passes[passIndex].live)
        {
            continue;
        }
        const PassRecord& pass = m_passes[passIndex];
        for (u32 useIndex = 0; useIndex < pass.useCount; ++useIndex)
        {
            ResourceRecord& resource = m_resources[m_uses[pass.useStart + useIndex].resourceIndex];
            resource.hasLiveUse = true;
            if (resource.firstPass == kInvalidIndex)
            {
                resource.firstPass = static_cast<u32>(passIndex);
            }
            resource.lastPass = static_cast<u32>(passIndex);
        }
    }

    for (usize resourceIndex = 0; resourceIndex < m_resources.Size(); ++resourceIndex)
    {
        ResourceRecord& resource = m_resources[resourceIndex];
        if (resource.imported || !resource.hasLiveUse)
        {
            continue;
        }

        u32 compatibleSlot = kInvalidIndex;
        for (usize slotIndex = 0; slotIndex < m_slots.Size(); ++slotIndex)
        {
            PhysicalSlot& slot = m_slots[slotIndex];
            const ResourceRecord& first = m_resources[slot.firstResource];
            if (slot.lastPass < resource.firstPass && Compatible(first, resource))
            {
                compatibleSlot = static_cast<u32>(slotIndex);
                break;
            }
        }
        if (compatibleSlot == kInvalidIndex)
        {
            PhysicalSlot slot{};
            slot.kind = resource.kind;
            slot.firstResource = static_cast<u32>(resourceIndex);
            slot.lastPass = resource.lastPass;
            if (!m_slots.TryPushBack(slot))
            {
                return Status<Error>::Err(
                    GraphError(ErrorCode::Internal, "render graph: alias slot allocation failed"));
            }
            compatibleSlot = static_cast<u32>(m_slots.Size() - 1);
        }
        else
        {
            m_slots[compatibleSlot].lastPass = resource.lastPass;
        }
        resource.physicalSlot = compatibleSlot;
    }

    m_compiled = true;
    return Status<Error>::Ok();
}

Status<Error> RenderGraph::Execute(cyber::rhi::CommandList& commandList) noexcept
{
    if (!m_compiled || m_destroyed || m_poisoned || m_settingUp || m_executionPending ||
        m_device == nullptr || !m_device->IsValid() || !commandList.IsRecording())
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument,
                       "render graph: execute requires a compiled graph and recording list"));
    }
    const Status<Error> allocated = EnsureTransientResources();
    if (!allocated)
    {
        return allocated;
    }
    m_executionPending = true;

    for (usize resourceIndex = 0; resourceIndex < m_resources.Size(); ++resourceIndex)
    {
        const ResourceRecord& resource = m_resources[resourceIndex];
        if (!resource.imported || !resource.hasLiveUse)
        {
            continue;
        }
        bool stateMatches = true;
        if (resource.kind == ResourceKind::Buffer)
        {
            stateMatches =
                m_device->BufferStateOf(resource.importedBuffer) == resource.currentState;
        }
        else
        {
            const cyber::rhi::TextureInfo info = m_device->TextureInfoOf(resource.importedTexture);
            stateMatches = info.valid;
            for (u32 mip = 0; mip < info.mipLevels && stateMatches; ++mip)
            {
                for (u32 layer = 0; layer < info.arrayLayers; ++layer)
                {
                    if (m_device->TextureStateOf(resource.importedTexture, mip, layer) !=
                        resource.currentState)
                    {
                        stateMatches = false;
                        break;
                    }
                }
            }
        }
        if (!stateMatches)
        {
            m_poisoned = true;
            return Status<Error>::Err(
                GraphError(ErrorCode::InvalidArgument,
                           "render graph: imported resource state changed externally"));
        }
    }

    for (usize slotIndex = 0; slotIndex < m_slots.Size(); ++slotIndex)
    {
        const PhysicalSlot& slot = m_slots[slotIndex];
        bool stateMatches = true;
        if (slot.kind == ResourceKind::Buffer)
        {
            stateMatches = m_device->BufferStateOf(slot.buffer) == slot.state;
        }
        else
        {
            const cyber::rhi::TextureInfo info = m_device->TextureInfoOf(slot.texture);
            stateMatches = info.valid;
            for (u32 mip = 0; mip < info.mipLevels && stateMatches; ++mip)
            {
                for (u32 layer = 0; layer < info.arrayLayers; ++layer)
                {
                    if (m_device->TextureStateOf(slot.texture, mip, layer) != slot.state)
                    {
                        stateMatches = false;
                        break;
                    }
                }
            }
        }
        if (!stateMatches)
        {
            m_poisoned = true;
            return Status<Error>::Err(
                GraphError(ErrorCode::InvalidArgument,
                           "render graph: transient resource state changed outside execution"));
        }
    }

    for (usize passIndex = 0; passIndex < m_passes.Size(); ++passIndex)
    {
        PassRecord& pass = m_passes[passIndex];
        if (!pass.live)
        {
            continue;
        }
        for (u32 useIndex = 0; useIndex < pass.useCount; ++useIndex)
        {
            const UseRecord& use = m_uses[pass.useStart + useIndex];
            const Status<Error> transitioned =
                Transition(use.resourceIndex, use.state, commandList);
            if (!transitioned)
            {
                m_poisoned = true;
                return transitioned;
            }
        }
        RenderPassContext context(*this, static_cast<u32>(passIndex), commandList);
        const Status<Error> executed = pass.execute(context, pass.userData);
        if (!executed)
        {
            m_poisoned = true;
            return executed;
        }
    }

    for (usize resourceIndex = 0; resourceIndex < m_resources.Size(); ++resourceIndex)
    {
        const ResourceRecord& resource = m_resources[resourceIndex];
        if (resource.exported && resource.finalState != ResourceState::Undefined &&
            resource.hasLiveUse)
        {
            const Status<Error> transitioned =
                Transition(static_cast<u32>(resourceIndex), resource.finalState, commandList);
            if (!transitioned)
            {
                m_poisoned = true;
                return transitioned;
            }
        }
    }
    return Status<Error>::Ok();
}

Status<Error> RenderGraph::AcknowledgeSubmission() noexcept
{
    if (!m_compiled || m_destroyed || m_poisoned || !m_executionPending)
    {
        return Status<Error>::Err(GraphError(
            ErrorCode::InvalidArgument,
            "render graph: no successful execution is awaiting submission acknowledgement"));
    }
    m_executionPending = false;
    return Status<Error>::Ok();
}

Status<Error> RenderGraph::Destroy() noexcept
{
    if (m_settingUp)
    {
        return Status<Error>::Err(GraphError(ErrorCode::InvalidArgument,
                                             "render graph: cannot destroy during pass setup"));
    }
    if (m_destroyed)
    {
        return Status<Error>::Ok();
    }
    if (m_device == nullptr)
    {
        m_destroyed = true;
        return Status<Error>::Ok();
    }

    const Status<Error> released = DestroyTransientSlots();
    if (!released)
    {
        m_poisoned = true;
        return released;
    }
    m_destroyed = true;
    ++m_generation;
    if (m_generation == 0)
    {
        m_generation = 1;
    }
    return Status<Error>::Ok();
}

u32 RenderGraph::PassCount() const noexcept
{
    return static_cast<u32>(m_passes.Size());
}

u32 RenderGraph::CulledPassCount() const noexcept
{
    u32 count = 0;
    for (usize index = 0; index < m_passes.Size(); ++index)
    {
        if (m_compiled && !m_passes[index].live)
        {
            ++count;
        }
    }
    return count;
}

u32 RenderGraph::TransientAllocationCount() const noexcept
{
    return static_cast<u32>(m_slots.Size());
}

u32 RenderGraph::AliasedResourceCount() const noexcept
{
    u32 count = 0;
    for (usize index = 0; index < m_resources.Size(); ++index)
    {
        const ResourceRecord& resource = m_resources[index];
        if (!resource.imported && resource.hasLiveUse && resource.physicalSlot != kInvalidIndex &&
            m_slots[resource.physicalSlot].firstResource != index)
        {
            ++count;
        }
    }
    return count;
}

Result<usize, Error> RenderGraph::WriteTextDump(Span<char> output) const noexcept
{
    if (!m_compiled || output.Data() == nullptr)
    {
        return Result<usize, Error>::Err(
            GraphError(ErrorCode::InvalidArgument,
                       "render graph: text dump needs a compiled graph and storage"));
    }
    auto write = [this](DumpWriter& writer) noexcept
    {
        writer.Append("RenderGraph passes=");
        writer.AppendNumber(PassCount());
        writer.Append(" culled=");
        writer.AppendNumber(CulledPassCount());
        writer.Append(" transient_slots=");
        writer.AppendNumber(TransientAllocationCount());
        writer.Append(" aliases=");
        writer.AppendNumber(AliasedResourceCount());
        writer.Append("\nResources:\n");
        for (usize index = 0; index < m_resources.Size(); ++index)
        {
            const ResourceRecord& resource = m_resources[index];
            writer.Append("  R");
            writer.AppendNumber(static_cast<u32>(index));
            writer.Append(" ");
            writer.Append(resource.name);
            writer.Append(resource.kind == ResourceKind::Texture ? " texture" : " buffer");
            writer.Append(resource.imported ? " imported" : " transient");
            writer.Append(resource.exported ? " exported" : "");
            if (resource.physicalSlot != kInvalidIndex)
            {
                writer.Append(" slot=");
                writer.AppendNumber(resource.physicalSlot);
            }
            writer.Append("\n");
        }
        writer.Append("Passes:\n");
        for (usize index = 0; index < m_passes.Size(); ++index)
        {
            const PassRecord& pass = m_passes[index];
            writer.Append("  P");
            writer.AppendNumber(static_cast<u32>(index));
            writer.Append(" ");
            writer.Append(pass.name);
            writer.Append(pass.live ? " live\n" : " culled\n");
            for (u32 useIndex = 0; useIndex < pass.useCount; ++useIndex)
            {
                const UseRecord& use = m_uses[pass.useStart + useIndex];
                writer.Append("    ");
                writer.Append(use.write ? "write R" : "read R");
                writer.AppendNumber(use.resourceIndex);
                writer.Append(" state=");
                writer.Append(cyber::rhi::ResourceStateName(use.state));
                writer.Append("\n");
            }
        }
        writer.Append("Dependencies:\n");
        for (usize index = 0; index < m_dependencies.Size(); ++index)
        {
            const Dependency& dependency = m_dependencies[index];
            if (m_passes[dependency.producer].live && m_passes[dependency.consumer].live)
            {
                writer.Append("  P");
                writer.AppendNumber(dependency.producer);
                writer.Append(" -> P");
                writer.AppendNumber(dependency.consumer);
                writer.Append("\n");
            }
        }
    };

    DumpWriter measure{};
    write(measure);
    if (measure.overflow || measure.size == ~usize{0} || output.Size() < measure.size + 1)
    {
        return Result<usize, Error>::Err(
            GraphError(ErrorCode::OutOfRange, "render graph: text dump buffer is too small"));
    }
    DumpWriter writer{output.Data(), output.Size() - 1, 0, false};
    write(writer);
    if (writer.overflow)
    {
        return Result<usize, Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: text dump exceeded measured capacity"));
    }
    output[writer.size] = '\0';
    return Result<usize, Error>::Ok(writer.size);
}

Result<usize, Error> RenderGraph::WriteGraphvizDot(Span<char> output) const noexcept
{
    if (!m_compiled || output.Data() == nullptr)
    {
        return Result<usize, Error>::Err(
            GraphError(ErrorCode::InvalidArgument,
                       "render graph: DOT dump needs a compiled graph and storage"));
    }
    auto write = [this](DumpWriter& writer) noexcept
    {
        writer.Append("digraph RenderGraph {\n  rankdir=LR;\n");
        for (usize passIndex = 0; passIndex < m_passes.Size(); ++passIndex)
        {
            const PassRecord& pass = m_passes[passIndex];
            if (!pass.live)
            {
                continue;
            }
            writer.Append("  p");
            writer.AppendNumber(static_cast<u32>(passIndex));
            writer.Append(" [shape=ellipse,label=\"");
            writer.AppendEscaped(pass.name);
            writer.Append("\"];\n");
        }
        for (usize resourceIndex = 0; resourceIndex < m_resources.Size(); ++resourceIndex)
        {
            const ResourceRecord& resource = m_resources[resourceIndex];
            if (!resource.hasLiveUse)
            {
                continue;
            }
            writer.Append("  r");
            writer.AppendNumber(static_cast<u32>(resourceIndex));
            writer.Append(" [shape=box,label=\"");
            writer.AppendEscaped(resource.name);
            writer.Append(resource.kind == ResourceKind::Texture ? "\\ntexture" : "\\nbuffer");
            if (resource.physicalSlot != kInvalidIndex)
            {
                writer.Append("\\nslot ");
                writer.AppendNumber(resource.physicalSlot);
            }
            writer.Append("\"];\n");
        }
        for (usize passIndex = 0; passIndex < m_passes.Size(); ++passIndex)
        {
            const PassRecord& pass = m_passes[passIndex];
            if (!pass.live)
            {
                continue;
            }
            for (u32 useIndex = 0; useIndex < pass.useCount; ++useIndex)
            {
                const UseRecord& use = m_uses[pass.useStart + useIndex];
                if (!m_resources[use.resourceIndex].hasLiveUse)
                {
                    continue;
                }
                writer.Append("  ");
                if (use.write)
                {
                    writer.Append("p");
                    writer.AppendNumber(static_cast<u32>(passIndex));
                    writer.Append(" -> r");
                    writer.AppendNumber(use.resourceIndex);
                }
                else
                {
                    writer.Append("r");
                    writer.AppendNumber(use.resourceIndex);
                    writer.Append(" -> p");
                    writer.AppendNumber(static_cast<u32>(passIndex));
                }
                writer.Append(" [label=\"");
                writer.Append(cyber::rhi::ResourceStateName(use.state));
                writer.Append("\"];\n");
            }
        }
        writer.Append("}\n");
    };

    DumpWriter measure{};
    write(measure);
    if (measure.overflow || measure.size == ~usize{0} || output.Size() < measure.size + 1)
    {
        return Result<usize, Error>::Err(
            GraphError(ErrorCode::OutOfRange, "render graph: DOT buffer is too small"));
    }
    DumpWriter writer{output.Data(), output.Size() - 1, 0, false};
    write(writer);
    if (writer.overflow)
    {
        return Result<usize, Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: DOT dump exceeded measured capacity"));
    }
    output[writer.size] = '\0';
    return Result<usize, Error>::Ok(writer.size);
}

Status<Error> RenderGraph::AddUse(u32 passIndex, GraphResourceHandle resource, ResourceState state,
                                  bool write) noexcept
{
    if (!m_settingUp || passIndex != m_setupPass || passIndex >= m_passes.Size() ||
        !IsValidResource(resource) || m_compiled || m_destroyed)
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: invalid resource declaration"));
    }
    ResourceRecord& record = m_resources[resource.index];
    const bool compatible = record.kind == ResourceKind::Buffer
                                ? SupportsState(record.bufferDesc.usage, state)
                                : SupportsState(record.textureDesc.usage, state);
    if (!compatible || (write ? !cyber::rhi::IsWriteState(state) : !cyber::rhi::IsReadState(state)))
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument,
                       "render graph: declared state is incompatible with resource"));
    }

    const PassRecord& pass = m_passes[passIndex];
    for (u32 index = 0; index < pass.useCount; ++index)
    {
        if (m_uses[pass.useStart + index].resourceIndex == resource.index)
        {
            return Status<Error>::Err(
                GraphError(ErrorCode::InvalidArgument,
                           "render graph: pass declares a resource more than once"));
        }
    }
    if (m_uses.Size() >= kInvalidIndex || pass.useCount == kInvalidIndex)
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::OutOfRange, "render graph: resource-use capacity exhausted"));
    }
    if (!m_uses.TryPushBack(UseRecord{resource.index, state, write}))
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::Internal, "render graph: resource-use allocation failed"));
    }
    m_passes[passIndex].useCount++;
    return Status<Error>::Ok();
}

bool RenderGraph::IsValidResource(GraphResourceHandle resource) const noexcept
{
    return resource.owner == this && resource.generation == m_generation &&
           resource.index < m_resources.Size() && !m_destroyed;
}

bool RenderGraph::IsDeclaredByPass(u32 passIndex, u32 resourceIndex) const noexcept
{
    if (passIndex >= m_passes.Size())
    {
        return false;
    }
    const PassRecord& pass = m_passes[passIndex];
    for (u32 index = 0; index < pass.useCount; ++index)
    {
        if (m_uses[pass.useStart + index].resourceIndex == resourceIndex)
        {
            return true;
        }
    }
    return false;
}

cyber::rhi::BufferHandle RenderGraph::ResolveBuffer(u32 resourceIndex) const noexcept
{
    if (resourceIndex >= m_resources.Size() ||
        m_resources[resourceIndex].kind != ResourceKind::Buffer)
    {
        return {};
    }
    const ResourceRecord& resource = m_resources[resourceIndex];
    return resource.imported
               ? resource.importedBuffer
               : (resource.physicalSlot < m_slots.Size() ? m_slots[resource.physicalSlot].buffer
                                                         : cyber::rhi::BufferHandle{});
}

cyber::rhi::TextureHandle RenderGraph::ResolveTexture(u32 resourceIndex) const noexcept
{
    if (resourceIndex >= m_resources.Size() ||
        m_resources[resourceIndex].kind != ResourceKind::Texture)
    {
        return {};
    }
    const ResourceRecord& resource = m_resources[resourceIndex];
    return resource.imported
               ? resource.importedTexture
               : (resource.physicalSlot < m_slots.Size() ? m_slots[resource.physicalSlot].texture
                                                         : cyber::rhi::TextureHandle{});
}

bool RenderGraph::Compatible(const ResourceRecord& lhs, const ResourceRecord& rhs) const noexcept
{
    if (lhs.kind != rhs.kind)
    {
        return false;
    }
    if (lhs.kind == ResourceKind::Buffer)
    {
        return lhs.bufferDesc.usage == rhs.bufferDesc.usage &&
               lhs.bufferDesc.sizeBytes == rhs.bufferDesc.sizeBytes &&
               lhs.bufferDesc.elementStride == rhs.bufferDesc.elementStride;
    }
    return lhs.textureDesc.dimension == rhs.textureDesc.dimension &&
           lhs.textureDesc.format == rhs.textureDesc.format &&
           lhs.textureDesc.usage == rhs.textureDesc.usage &&
           lhs.textureDesc.width == rhs.textureDesc.width &&
           lhs.textureDesc.height == rhs.textureDesc.height &&
           lhs.textureDesc.depth == rhs.textureDesc.depth &&
           lhs.textureDesc.arrayLayers == rhs.textureDesc.arrayLayers &&
           lhs.textureDesc.mipLevels == rhs.textureDesc.mipLevels &&
           lhs.textureDesc.sampleCount == rhs.textureDesc.sampleCount;
}

Status<Error> RenderGraph::EnsureTransientResources() noexcept
{
    for (usize index = 0; index < m_slots.Size(); ++index)
    {
        PhysicalSlot& slot = m_slots[index];
        const ResourceRecord& resource = m_resources[slot.firstResource];
        if (slot.kind == ResourceKind::Buffer && !slot.buffer.IsValid())
        {
            cyber::rhi::BufferDesc desc = resource.bufferDesc;
            desc.label = resource.name;
            auto created = m_device->CreateBuffer(desc);
            if (!created)
            {
                const Error error = created.Error();
                const Status<Error> cleanup = DestroyTransientSlots();
                if (!cleanup)
                {
                    m_poisoned = true;
                    return cleanup;
                }
                return Status<Error>::Err(error);
            }
            slot.buffer = created.Value();
        }
        else if (slot.kind == ResourceKind::Texture && !slot.texture.IsValid())
        {
            cyber::rhi::TextureDesc desc = resource.textureDesc;
            desc.label = resource.name;
            auto created = m_device->CreateTexture(desc);
            if (!created)
            {
                const Error error = created.Error();
                const Status<Error> cleanup = DestroyTransientSlots();
                if (!cleanup)
                {
                    m_poisoned = true;
                    return cleanup;
                }
                return Status<Error>::Err(error);
            }
            slot.texture = created.Value();
        }
    }
    return Status<Error>::Ok();
}

Status<Error> RenderGraph::DestroyTransientSlots() noexcept
{
    Error firstError{};
    bool failed = false;
    for (usize index = m_slots.Size(); index > 0; --index)
    {
        PhysicalSlot& slot = m_slots[index - 1];
        if (slot.buffer.IsValid())
        {
            const Status<Error> result = m_device->DestroyBuffer(slot.buffer);
            if (result)
            {
                slot.buffer = {};
            }
            else if (!failed)
            {
                firstError = result.Error();
                failed = true;
            }
        }
        if (slot.texture.IsValid())
        {
            const Status<Error> result = m_device->DestroyTexture(slot.texture);
            if (result)
            {
                slot.texture = {};
            }
            else if (!failed)
            {
                firstError = result.Error();
                failed = true;
            }
        }
        if (!slot.buffer.IsValid() && !slot.texture.IsValid())
        {
            slot.state = ResourceState::Undefined;
        }
    }
    return failed ? Status<Error>::Err(firstError) : Status<Error>::Ok();
}

Status<Error> RenderGraph::Transition(u32 resourceIndex, ResourceState target,
                                      cyber::rhi::CommandList& commandList) noexcept
{
    if (resourceIndex >= m_resources.Size())
    {
        return Status<Error>::Err(
            GraphError(ErrorCode::InvalidArgument, "render graph: transition resource is invalid"));
    }
    ResourceRecord& resource = m_resources[resourceIndex];
    PhysicalSlot* slot = !resource.imported && resource.physicalSlot < m_slots.Size()
                             ? &m_slots[resource.physicalSlot]
                             : nullptr;
    ResourceState& state = slot == nullptr ? resource.currentState : slot->state;
    if (state == target)
    {
        return Status<Error>::Ok();
    }

    if (resource.kind == ResourceKind::Buffer)
    {
        const cyber::rhi::BufferHandle buffer = ResolveBuffer(resourceIndex);
        const cyber::rhi::BufferBarrier barrier{buffer, state, target};
        const Status<Error> result =
            commandList.Barrier(Span<const cyber::rhi::BufferBarrier>(&barrier, 1));
        if (result)
        {
            state = target;
        }
        return result;
    }
    const cyber::rhi::TextureHandle texture = ResolveTexture(resourceIndex);
    cyber::rhi::SubresourceRange range{};
    const cyber::rhi::FormatInfo format = cyber::rhi::FormatInfoOf(resource.textureDesc.format);
    range.aspect = format.depth ? (format.stencil ? cyber::rhi::TextureAspect::DepthStencil
                                                  : cyber::rhi::TextureAspect::Depth)
                                : cyber::rhi::TextureAspect::Color;
    const cyber::rhi::TextureBarrier barrier{texture, range, state, target};
    const Status<Error> result =
        commandList.Barrier({}, Span<const cyber::rhi::TextureBarrier>(&barrier, 1));
    if (result)
    {
        state = target;
    }
    return result;
}

}  // namespace cyber::renderer
