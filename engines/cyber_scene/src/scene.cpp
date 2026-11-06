#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/slot_map.h>
#include <cyber/scene/scene.h>

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

namespace cyber::scene
{
namespace
{

using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::core::f32;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::SlotMap;

constexpr u32 kCreateCommand = 0;
constexpr u32 kUpdateCommand = 1;
constexpr u32 kDestroyCommand = 2;

[[nodiscard]] Error MakeError(ErrorCode code, const char* message) noexcept
{
    return Error{code, message};
}

[[nodiscard]] bool FitsAllocation(usize count, usize elementSize) noexcept
{
    return count == 0 || elementSize <= std::numeric_limits<usize>::max() / count;
}

[[nodiscard]] bool IsFinite(f32 value) noexcept
{
    return std::isfinite(value);
}

[[nodiscard]] bool IsFinite(const cyber::math::Vec3& value) noexcept
{
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

[[nodiscard]] bool IsFinite(const cyber::math::Mat4& value) noexcept
{
    for (usize column = 0; column < 4; ++column)
    {
        const cyber::math::Vec4& entry = value[column];
        if (!IsFinite(entry.x) || !IsFinite(entry.y) || !IsFinite(entry.z) || !IsFinite(entry.w))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsAffine(const cyber::math::Mat4& value) noexcept
{
    return value[0].w == 0.0f && value[1].w == 0.0f && value[2].w == 0.0f && value[3].w == 1.0f;
}

[[nodiscard]] bool IsValidBounds(const cyber::math::Aabb& bounds) noexcept
{
    return !bounds.IsEmpty() && IsFinite(bounds.Min()) && IsFinite(bounds.Max()) &&
           bounds.Min().x <= bounds.Max().x && bounds.Min().y <= bounds.Max().y &&
           bounds.Min().z <= bounds.Max().z;
}

[[nodiscard]] cyber::math::Aabb TransformBounds(const cyber::math::Mat4& transform,
                                                const cyber::math::Aabb& localBounds) noexcept;

[[nodiscard]] bool IsValid(const MeshInstanceDesc& desc) noexcept
{
    if (desc.meshId == 0 || desc.materialId == 0 || !IsFinite(desc.transform) ||
        !IsAffine(desc.transform) || !IsValidBounds(desc.localBounds) ||
        !IsValidBounds(TransformBounds(desc.transform, desc.localBounds)) || desc.lodCount == 0 ||
        desc.lodCount > kMaximumMeshLods)
    {
        return false;
    }
    for (u32 index = 0; index + 1 < desc.lodCount; ++index)
    {
        if (!IsFinite(desc.lodPixelThresholds[index]) || desc.lodPixelThresholds[index] <= 0.0f ||
            (index > 0 && desc.lodPixelThresholds[index - 1] <= desc.lodPixelThresholds[index]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool IsValid(const PointLightDesc& desc) noexcept
{
    if (!IsFinite(desc.transform) || !IsAffine(desc.transform) || !IsFinite(desc.color) ||
        desc.color.x < 0.0f || desc.color.y < 0.0f || desc.color.z < 0.0f ||
        !IsFinite(desc.intensity) || desc.intensity < 0.0f || !IsFinite(desc.range) ||
        desc.range <= 0.0f)
    {
        return false;
    }
    const cyber::math::Aabb localBounds = cyber::math::Aabb::FromCenterExtents(
        cyber::math::Vec3::Zero(), cyber::math::Vec3{desc.range});
    return IsValidBounds(TransformBounds(desc.transform, localBounds));
}

[[nodiscard]] bool IsValid(const CameraProxyDesc& desc) noexcept
{
    return IsFinite(desc.transform) && IsAffine(desc.transform) && IsFinite(desc.viewProjection);
}

[[nodiscard]] cyber::math::Aabb TransformBounds(const cyber::math::Mat4& transform,
                                                const cyber::math::Aabb& localBounds) noexcept
{
    const cyber::math::Vec3 center = transform.TransformPoint(localBounds.Center());
    const cyber::math::Vec3 extents = localBounds.Extents();
    const cyber::math::Vec3 worldExtents{cyber::math::Abs(transform[0].x) * extents.x +
                                             cyber::math::Abs(transform[1].x) * extents.y +
                                             cyber::math::Abs(transform[2].x) * extents.z,
                                         cyber::math::Abs(transform[0].y) * extents.x +
                                             cyber::math::Abs(transform[1].y) * extents.y +
                                             cyber::math::Abs(transform[2].y) * extents.z,
                                         cyber::math::Abs(transform[0].z) * extents.x +
                                             cyber::math::Abs(transform[1].z) * extents.y +
                                             cyber::math::Abs(transform[2].z) * extents.z};
    return cyber::math::Aabb::FromCenterExtents(center, worldExtents);
}

struct SceneCommand
{
    u32 version = kSceneCommandVersion;
    u32 operation = kCreateCommand;
    ProxyHandle handle{};
    ProxyKind kind = ProxyKind::MeshInstance;
    MeshInstanceDesc mesh{};
    PointLightDesc light{};
    CameraProxyDesc camera{};
};

static_assert(std::is_trivially_copyable_v<SceneCommand>);

struct ProxyRecord
{
    ProxyKind kind = ProxyKind::MeshInstance;
    cyber::math::Mat4 transform{};
    cyber::math::Aabb localBounds{};
    cyber::math::Aabb worldBounds{};
    u64 meshId = 0;
    u64 materialId = 0;
    u32 lodCount = 1;
    f32 lodPixelThresholds[kMaximumLodThresholds]{};
    cyber::math::Vec3 lightColor{};
    f32 lightIntensity = 0.0f;
    f32 lightRange = 0.0f;
    cyber::math::Mat4 viewProjection{};
    u8 dirtyFlags = 0;
};

struct SceneSlot
{
    u32 generation = 1;
    u32 nextFree = cyber::memory::kInvalidHandleIndex;
    bool occupied = false;
    ProxyRecord proxy{};
};

struct ValidationSlot
{
    u32 generation = 1;
    u32 nextFree = cyber::memory::kInvalidHandleIndex;
    ProxyKind kind = ProxyKind::MeshInstance;
    bool occupied = false;
};

struct CullSlot
{
    ProxyHandle handle{};
    ProxyKind kind = ProxyKind::MeshInstance;
    u32 lod = 0;
    bool visible = false;
};

struct CullContext
{
    const detail::SceneImpl* impl = nullptr;
    const SceneView* view = nullptr;
    CullSlot* output = nullptr;
};

[[nodiscard]] ProxyRecord MakeRecord(const MeshInstanceDesc& desc) noexcept
{
    ProxyRecord record{};
    record.kind = ProxyKind::MeshInstance;
    record.transform = desc.transform;
    record.localBounds = desc.localBounds;
    record.worldBounds = TransformBounds(desc.transform, desc.localBounds);
    record.meshId = desc.meshId;
    record.materialId = desc.materialId;
    record.lodCount = desc.lodCount;
    for (u32 index = 0; index < kMaximumLodThresholds; ++index)
    {
        record.lodPixelThresholds[index] = desc.lodPixelThresholds[index];
    }
    record.dirtyFlags = kProxyDirtyTransform | kProxyDirtyBounds;
    return record;
}

[[nodiscard]] ProxyRecord MakeRecord(const PointLightDesc& desc) noexcept
{
    ProxyRecord record{};
    record.kind = ProxyKind::PointLight;
    record.transform = desc.transform;
    record.localBounds = cyber::math::Aabb::FromCenterExtents(cyber::math::Vec3::Zero(),
                                                              cyber::math::Vec3{desc.range});
    record.worldBounds = TransformBounds(desc.transform, record.localBounds);
    record.lightColor = desc.color;
    record.lightIntensity = desc.intensity;
    record.lightRange = desc.range;
    record.dirtyFlags = kProxyDirtyTransform | kProxyDirtyBounds;
    return record;
}

[[nodiscard]] ProxyRecord MakeRecord(const CameraProxyDesc& desc) noexcept
{
    ProxyRecord record{};
    record.kind = ProxyKind::Camera;
    record.transform = desc.transform;
    record.viewProjection = desc.viewProjection;
    record.dirtyFlags = kProxyDirtyTransform;
    return record;
}

[[nodiscard]] bool UpdateRecord(ProxyRecord& record, const MeshInstanceDesc& desc) noexcept
{
    if (record.kind != ProxyKind::MeshInstance || !IsValid(desc))
    {
        return false;
    }
    record = MakeRecord(desc);
    return true;
}

[[nodiscard]] bool UpdateRecord(ProxyRecord& record, const PointLightDesc& desc) noexcept
{
    if (record.kind != ProxyKind::PointLight || !IsValid(desc))
    {
        return false;
    }
    record = MakeRecord(desc);
    return true;
}

[[nodiscard]] bool UpdateRecord(ProxyRecord& record, const CameraProxyDesc& desc) noexcept
{
    if (record.kind != ProxyKind::Camera || !IsValid(desc))
    {
        return false;
    }
    record = MakeRecord(desc);
    return true;
}

[[nodiscard]] bool IsValidView(const SceneView& view) noexcept
{
    if (!IsFinite(view.cameraPosition) || !IsFinite(view.projectionScalePixels) ||
        view.projectionScalePixels <= 0.0f || !IsFinite(view.nearDistance) ||
        view.nearDistance <= 0.0f)
    {
        return false;
    }
    for (const cyber::math::Plane& plane : view.frustum.planes)
    {
        if (!IsFinite(plane.normal) || !IsFinite(plane.distance))
        {
            return false;
        }
        if (cyber::math::LengthSquared(plane.normal) <= 0.0f)
        {
            return false;
        }
    }
    return true;
}

}  // namespace

namespace detail
{

struct SceneCommandWriterImpl
{
    SceneCommandWriterImpl(Allocator& sourceAllocator, u32 proxyLimit, u32 commandLimit) noexcept
        : allocator(&sourceAllocator), handles(sourceAllocator), commands(sourceAllocator),
          maxProxies(proxyLimit), maxCommands(commandLimit)
    {
    }

    Allocator* allocator = nullptr;
    SlotMap<ProxyKind, ProxyTag> handles;
    DynamicArray<SceneCommand> commands;
    u32 maxProxies = 0;
    u32 maxCommands = 0;
    bool sealed = false;
};

struct SceneImpl
{
    SceneImpl(Allocator& sourceAllocator, u32 proxyLimit, u32 commandLimit) noexcept
        : allocator(&sourceAllocator), slots(sourceAllocator), cullSlots(sourceAllocator),
          validationSlots(sourceAllocator), validationStamps(sourceAllocator),
          maxProxies(proxyLimit), maxCommands(commandLimit)
    {
    }

    Allocator* allocator = nullptr;
    DynamicArray<SceneSlot> slots;
    DynamicArray<CullSlot> cullSlots;
    DynamicArray<ValidationSlot> validationSlots;
    DynamicArray<u32> validationStamps;
    u32 maxProxies = 0;
    u32 maxCommands = 0;
    u32 freeHead = cyber::memory::kInvalidHandleIndex;
    u32 validationFreeHead = cyber::memory::kInvalidHandleIndex;
    u32 validationEpoch = 0;
    usize validationSize = 0;
    usize liveCount = 0;
};

}  // namespace detail

VisibilityList::VisibilityList(Allocator& allocator) noexcept : m_allocator(&allocator) {}

VisibilityList::~VisibilityList()
{
    Destroy();
}

VisibilityList::VisibilityList(VisibilityList&& other) noexcept
    : m_allocator(other.m_allocator), m_entries(other.m_entries), m_size(other.m_size),
      m_capacity(other.m_capacity)
{
    other.m_entries = nullptr;
    other.m_size = 0;
    other.m_capacity = 0;
}

VisibilityList& VisibilityList::operator=(VisibilityList&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        m_allocator = other.m_allocator;
        m_entries = other.m_entries;
        m_size = other.m_size;
        m_capacity = other.m_capacity;
        other.m_entries = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }
    return *this;
}

usize VisibilityList::Size() const noexcept
{
    return m_size;
}

const VisibleProxy* VisibilityList::Data() const noexcept
{
    return m_entries;
}

Span<const VisibleProxy> VisibilityList::Entries() const noexcept
{
    return Span<const VisibleProxy>{m_entries, m_size};
}

void VisibilityList::Destroy() noexcept
{
    if (m_entries != nullptr)
    {
        for (usize index = 0; index < m_size; ++index)
        {
            std::destroy_at(m_entries + index);
        }
        m_allocator->Deallocate(m_entries);
    }
    m_entries = nullptr;
    m_size = 0;
    m_capacity = 0;
}

SceneCommandWriter::SceneCommandWriter(detail::SceneCommandWriterImpl* impl) noexcept : m_impl(impl)
{
}

SceneCommandWriter::~SceneCommandWriter()
{
    DestroyImpl();
}

SceneCommandWriter::SceneCommandWriter(SceneCommandWriter&& other) noexcept : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

SceneCommandWriter& SceneCommandWriter::operator=(SceneCommandWriter&& other) noexcept
{
    if (this != &other)
    {
        DestroyImpl();
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Result<SceneCommandWriter, Error> SceneCommandWriter::Create(const SceneCommandWriterDesc& desc,
                                                             Allocator& allocator) noexcept
{
    if (desc.maxProxies == 0 || desc.maxProxies == cyber::memory::kInvalidHandleIndex ||
        desc.maxCommandsPerPacket == 0)
    {
        return Result<SceneCommandWriter, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene command writer limits must be non-zero"));
    }
    if (!FitsAllocation(desc.maxCommandsPerPacket, sizeof(SceneCommand)))
    {
        return Result<SceneCommandWriter, Error>::Err(
            MakeError(ErrorCode::Overflow, "scene command packet size overflows"));
    }

    void* storage = allocator.Allocate(sizeof(detail::SceneCommandWriterImpl),
                                       alignof(detail::SceneCommandWriterImpl));
    if (storage == nullptr)
    {
        return Result<SceneCommandWriter, Error>::Err(
            MakeError(ErrorCode::Internal, "scene command writer allocation failed"));
    }
    auto* impl = std::construct_at(static_cast<detail::SceneCommandWriterImpl*>(storage), allocator,
                                   desc.maxProxies, desc.maxCommandsPerPacket);
    if (!impl->handles.TryReserve(desc.maxProxies) ||
        !impl->commands.TryReserve(desc.maxCommandsPerPacket))
    {
        std::destroy_at(impl);
        allocator.Deallocate(storage);
        return Result<SceneCommandWriter, Error>::Err(
            MakeError(ErrorCode::Internal, "scene command writer storage allocation failed"));
    }
    return Result<SceneCommandWriter, Error>::Ok(SceneCommandWriter{impl});
}

namespace
{

[[nodiscard]] bool CanAppend(const detail::SceneCommandWriterImpl& impl) noexcept
{
    return !impl.sealed && impl.commands.Size() < impl.maxCommands;
}

[[nodiscard]] Status<Error> WriterError(bool sealed) noexcept
{
    return Status<Error>::Err(
        MakeError(sealed ? ErrorCode::Busy : ErrorCode::OutOfRange,
                  sealed ? "scene command packet is sealed" : "scene command packet is full"));
}

template <typename Descriptor, typename Builder>
[[nodiscard]] Result<ProxyHandle, Error> CreateProxy(detail::SceneCommandWriterImpl* impl,
                                                     ProxyKind kind, const Descriptor& desc,
                                                     Builder&& build) noexcept
{
    if (impl == nullptr)
    {
        return Result<ProxyHandle, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene command writer is empty"));
    }
    if (!IsValid(desc))
    {
        return Result<ProxyHandle, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "invalid scene proxy descriptor"));
    }
    if (!CanAppend(*impl))
    {
        return Result<ProxyHandle, Error>::Err(MakeError(
            impl->sealed ? ErrorCode::Busy : ErrorCode::OutOfRange,
            impl->sealed ? "scene command packet is sealed" : "scene command packet is full"));
    }
    if (impl->handles.Size() >= impl->maxProxies)
    {
        return Result<ProxyHandle, Error>::Err(
            MakeError(ErrorCode::OutOfRange, "scene proxy handle capacity is exhausted"));
    }

    const ProxyHandle handle = impl->handles.Insert(kind);
    if (!handle.IsValid())
    {
        return Result<ProxyHandle, Error>::Err(
            MakeError(ErrorCode::Internal, "scene proxy handle allocation failed"));
    }
    SceneCommand command{};
    command.operation = kCreateCommand;
    command.handle = handle;
    command.kind = kind;
    build(command, desc);
    if (!impl->commands.TryPushBack(command))
    {
        (void)impl->handles.Remove(handle);
        return Result<ProxyHandle, Error>::Err(
            MakeError(ErrorCode::Internal, "scene command storage is exhausted"));
    }
    return Result<ProxyHandle, Error>::Ok(handle);
}

template <typename Descriptor, typename Builder>
[[nodiscard]] Status<Error> UpdateProxy(detail::SceneCommandWriterImpl* impl, ProxyHandle handle,
                                        ProxyKind kind, const Descriptor& desc,
                                        Builder&& build) noexcept
{
    if (impl == nullptr)
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene command writer is empty"));
    }
    if (!IsValid(desc))
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "invalid scene proxy descriptor"));
    }
    if (!CanAppend(*impl))
    {
        return WriterError(impl->sealed);
    }
    const ProxyKind* current = impl->handles.Get(handle);
    if (current == nullptr || *current != kind)
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::NotFound, "scene proxy handle is stale or has a different kind"));
    }
    SceneCommand command{};
    command.operation = kUpdateCommand;
    command.handle = handle;
    command.kind = kind;
    build(command, desc);
    if (!impl->commands.TryPushBack(command))
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::Internal, "scene command storage is exhausted"));
    }
    return Status<Error>::Ok();
}

}  // namespace

Result<ProxyHandle, Error>
SceneCommandWriter::CreateMeshInstance(const MeshInstanceDesc& desc) noexcept
{
    return CreateProxy(m_impl, ProxyKind::MeshInstance, desc,
                       [](SceneCommand& command, const MeshInstanceDesc& source) noexcept
                       { command.mesh = source; });
}

Status<Error> SceneCommandWriter::UpdateMeshInstance(ProxyHandle handle,
                                                     const MeshInstanceDesc& desc) noexcept
{
    return UpdateProxy(m_impl, handle, ProxyKind::MeshInstance, desc,
                       [](SceneCommand& command, const MeshInstanceDesc& source) noexcept
                       { command.mesh = source; });
}

Result<ProxyHandle, Error> SceneCommandWriter::CreatePointLight(const PointLightDesc& desc) noexcept
{
    return CreateProxy(m_impl, ProxyKind::PointLight, desc,
                       [](SceneCommand& command, const PointLightDesc& source) noexcept
                       { command.light = source; });
}

Status<Error> SceneCommandWriter::UpdatePointLight(ProxyHandle handle,
                                                   const PointLightDesc& desc) noexcept
{
    return UpdateProxy(m_impl, handle, ProxyKind::PointLight, desc,
                       [](SceneCommand& command, const PointLightDesc& source) noexcept
                       { command.light = source; });
}

Result<ProxyHandle, Error> SceneCommandWriter::CreateCamera(const CameraProxyDesc& desc) noexcept
{
    return CreateProxy(m_impl, ProxyKind::Camera, desc,
                       [](SceneCommand& command, const CameraProxyDesc& source) noexcept
                       { command.camera = source; });
}

Status<Error> SceneCommandWriter::UpdateCamera(ProxyHandle handle,
                                               const CameraProxyDesc& desc) noexcept
{
    return UpdateProxy(m_impl, handle, ProxyKind::Camera, desc,
                       [](SceneCommand& command, const CameraProxyDesc& source) noexcept
                       { command.camera = source; });
}

Status<Error> SceneCommandWriter::Destroy(ProxyHandle handle) noexcept
{
    if (m_impl == nullptr)
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene command writer is empty"));
    }
    if (!CanAppend(*m_impl))
    {
        return WriterError(m_impl->sealed);
    }
    const ProxyKind* kind = m_impl->handles.Get(handle);
    if (kind == nullptr)
    {
        return Status<Error>::Err(MakeError(ErrorCode::NotFound, "scene proxy handle is stale"));
    }
    SceneCommand command{};
    command.operation = kDestroyCommand;
    command.handle = handle;
    command.kind = *kind;
    if (!m_impl->commands.TryPushBack(command))
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::Internal, "scene command storage is exhausted"));
    }
    if (!m_impl->handles.Remove(handle))
    {
        m_impl->commands.PopBack();
        return Status<Error>::Err(
            MakeError(ErrorCode::Internal, "scene proxy handle removal failed"));
    }
    return Status<Error>::Ok();
}

Span<const Byte> SceneCommandWriter::Seal() noexcept
{
    if (m_impl == nullptr)
    {
        return {};
    }
    m_impl->sealed = true;
    return Span<const Byte>{reinterpret_cast<const Byte*>(m_impl->commands.Data()),
                            m_impl->commands.Size() * sizeof(SceneCommand)};
}

Status<Error> SceneCommandWriter::Commit() noexcept
{
    if (m_impl == nullptr || !m_impl->sealed)
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "no sealed scene command packet to commit"));
    }
    m_impl->commands.Clear();
    m_impl->sealed = false;
    return Status<Error>::Ok();
}

usize SceneCommandWriter::MaxPacketBytes() const noexcept
{
    return m_impl == nullptr ? 0 : static_cast<usize>(m_impl->maxCommands) * sizeof(SceneCommand);
}

u32 SceneCommandWriter::CommandCount() const noexcept
{
    return m_impl == nullptr ? 0 : static_cast<u32>(m_impl->commands.Size());
}

void SceneCommandWriter::DestroyImpl() noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    Allocator* allocator = m_impl->allocator;
    std::destroy_at(m_impl);
    allocator->Deallocate(m_impl);
    m_impl = nullptr;
}

Scene::Scene(detail::SceneImpl* impl) noexcept : m_impl(impl) {}

Scene::~Scene()
{
    Destroy();
}

Scene::Scene(Scene&& other) noexcept : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

Scene& Scene::operator=(Scene&& other) noexcept
{
    if (this != &other)
    {
        Destroy();
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

Result<Scene, Error> Scene::Create(const SceneDesc& desc, Allocator& allocator) noexcept
{
    if (desc.maxProxies == 0 || desc.maxProxies == cyber::memory::kInvalidHandleIndex ||
        desc.maxCommandsPerPacket == 0)
    {
        return Result<Scene, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene limits must be non-zero"));
    }
    if (!FitsAllocation(desc.maxProxies, sizeof(SceneSlot)) ||
        !FitsAllocation(desc.maxProxies, sizeof(CullSlot)) ||
        !FitsAllocation(desc.maxProxies, sizeof(ValidationSlot)) ||
        !FitsAllocation(desc.maxProxies, sizeof(u32)))
    {
        return Result<Scene, Error>::Err(
            MakeError(ErrorCode::Overflow, "scene storage size overflows"));
    }

    void* storage = allocator.Allocate(sizeof(detail::SceneImpl), alignof(detail::SceneImpl));
    if (storage == nullptr)
    {
        return Result<Scene, Error>::Err(MakeError(ErrorCode::Internal, "scene allocation failed"));
    }
    auto* impl = std::construct_at(static_cast<detail::SceneImpl*>(storage), allocator,
                                   desc.maxProxies, desc.maxCommandsPerPacket);
    if (!impl->slots.TryReserve(desc.maxProxies) || !impl->cullSlots.TryReserve(desc.maxProxies) ||
        !impl->cullSlots.TryResize(desc.maxProxies) ||
        !impl->validationSlots.TryReserve(desc.maxProxies) ||
        !impl->validationSlots.TryResize(desc.maxProxies) ||
        !impl->validationStamps.TryReserve(desc.maxProxies) ||
        !impl->validationStamps.TryResize(desc.maxProxies))
    {
        std::destroy_at(impl);
        allocator.Deallocate(storage);
        return Result<Scene, Error>::Err(
            MakeError(ErrorCode::Internal, "scene storage allocation failed"));
    }
    return Result<Scene, Error>::Ok(Scene{impl});
}

namespace
{

[[nodiscard]] ProxyHandle NextSceneHandle(const detail::SceneImpl& impl) noexcept
{
    if (impl.freeHead != cyber::memory::kInvalidHandleIndex)
    {
        const SceneSlot& slot = impl.slots[impl.freeHead];
        return ProxyHandle{impl.freeHead, slot.generation};
    }
    if (impl.slots.Size() >= impl.maxProxies)
    {
        return {};
    }
    return ProxyHandle{static_cast<u32>(impl.slots.Size()), 1};
}

[[nodiscard]] SceneSlot* GetSceneSlot(detail::SceneImpl& impl, ProxyHandle handle) noexcept
{
    if (!handle.IsValid() || handle.index >= impl.slots.Size())
    {
        return nullptr;
    }
    SceneSlot& slot = impl.slots[handle.index];
    return slot.occupied && slot.generation == handle.generation ? &slot : nullptr;
}

[[nodiscard]] bool ApplyCreate(detail::SceneImpl& impl, const SceneCommand& command) noexcept
{
    switch (command.kind)
    {
    case ProxyKind::MeshInstance:
        if (!IsValid(command.mesh))
        {
            return false;
        }
        break;
    case ProxyKind::PointLight:
        if (!IsValid(command.light))
        {
            return false;
        }
        break;
    case ProxyKind::Camera:
        if (!IsValid(command.camera))
        {
            return false;
        }
        break;
    default:
        return false;
    }

    const ProxyHandle next = NextSceneHandle(impl);
    if (next != command.handle)
    {
        return false;
    }
    if (impl.freeHead != cyber::memory::kInvalidHandleIndex)
    {
        SceneSlot& slot = impl.slots[impl.freeHead];
        impl.freeHead = slot.nextFree;
        slot.nextFree = cyber::memory::kInvalidHandleIndex;
        slot.occupied = true;
    }
    else
    {
        SceneSlot* slot = impl.slots.TryEmplaceBack();
        if (slot == nullptr)
        {
            return false;
        }
        slot->generation = 1;
        slot->nextFree = cyber::memory::kInvalidHandleIndex;
        slot->occupied = true;
    }
    SceneSlot& slot = impl.slots[command.handle.index];
    switch (command.kind)
    {
    case ProxyKind::MeshInstance:
        slot.proxy = MakeRecord(command.mesh);
        break;
    case ProxyKind::PointLight:
        slot.proxy = MakeRecord(command.light);
        break;
    case ProxyKind::Camera:
        slot.proxy = MakeRecord(command.camera);
        break;
    default:
        return false;
    }
    ++impl.liveCount;
    return true;
}

[[nodiscard]] bool ApplyUpdate(detail::SceneImpl& impl, const SceneCommand& command) noexcept
{
    SceneSlot* slot = GetSceneSlot(impl, command.handle);
    if (slot == nullptr || slot->proxy.kind != command.kind)
    {
        return false;
    }
    switch (command.kind)
    {
    case ProxyKind::MeshInstance:
        return UpdateRecord(slot->proxy, command.mesh);
    case ProxyKind::PointLight:
        return UpdateRecord(slot->proxy, command.light);
    case ProxyKind::Camera:
        return UpdateRecord(slot->proxy, command.camera);
    }
    return false;
}

[[nodiscard]] bool ApplyDestroy(detail::SceneImpl& impl, const SceneCommand& command) noexcept
{
    SceneSlot* slot = GetSceneSlot(impl, command.handle);
    if (slot == nullptr || slot->proxy.kind != command.kind)
    {
        return false;
    }
    slot->occupied = false;
    ++slot->generation;
    if (slot->generation == 0)
    {
        slot->generation = 1;
    }
    slot->nextFree = impl.freeHead;
    impl.freeHead = command.handle.index;
    --impl.liveCount;
    return true;
}

[[nodiscard]] ValidationSlot& ValidationAt(detail::SceneImpl& impl, u32 index) noexcept
{
    ValidationSlot& validation = impl.validationSlots[index];
    if (impl.validationStamps[index] != impl.validationEpoch)
    {
        if (index < impl.slots.Size())
        {
            const SceneSlot& source = impl.slots[index];
            validation = ValidationSlot{source.generation, source.nextFree, source.proxy.kind,
                                        source.occupied};
        }
        else
        {
            validation = ValidationSlot{};
        }
        impl.validationStamps[index] = impl.validationEpoch;
    }
    return validation;
}

[[nodiscard]] Error ValidateCommandPacket(detail::SceneImpl& impl,
                                          Span<const Byte> commands) noexcept
{
    impl.validationSize = impl.slots.Size();
    impl.validationFreeHead = impl.freeHead;
    ++impl.validationEpoch;
    if (impl.validationEpoch == 0)
    {
        for (usize index = 0; index < impl.validationStamps.Size(); ++index)
        {
            impl.validationStamps[index] = 0;
        }
        impl.validationEpoch = 1;
    }

    const usize count = commands.Size() / sizeof(SceneCommand);
    if (count == 0)
    {
        return Error{ErrorCode::Ok, nullptr};
    }
    for (usize index = 0; index < count; ++index)
    {
        SceneCommand command{};
        std::memcpy(&command, commands.Data() + index * sizeof(SceneCommand), sizeof(command));
        if (command.version != kSceneCommandVersion || !command.handle.IsValid())
        {
            return MakeError(ErrorCode::ParseError, "scene command version or handle is invalid");
        }
        if (command.operation == kCreateCommand)
        {
            switch (command.kind)
            {
            case ProxyKind::MeshInstance:
                if (!IsValid(command.mesh))
                {
                    return MakeError(ErrorCode::InvalidArgument, "invalid mesh proxy command");
                }
                break;
            case ProxyKind::PointLight:
                if (!IsValid(command.light))
                {
                    return MakeError(ErrorCode::InvalidArgument, "invalid light proxy command");
                }
                break;
            case ProxyKind::Camera:
                if (!IsValid(command.camera))
                {
                    return MakeError(ErrorCode::InvalidArgument, "invalid camera proxy command");
                }
                break;
            default:
                return MakeError(ErrorCode::ParseError, "scene command has an unknown proxy kind");
            }
            ProxyHandle expected{};
            if (impl.validationFreeHead != cyber::memory::kInvalidHandleIndex)
            {
                ValidationSlot& freeSlot = ValidationAt(impl, impl.validationFreeHead);
                expected = ProxyHandle{impl.validationFreeHead, freeSlot.generation};
            }
            else if (impl.validationSize < impl.maxProxies)
            {
                expected = ProxyHandle{static_cast<u32>(impl.validationSize), 1};
            }
            if (expected != command.handle)
            {
                return MakeError(ErrorCode::InvalidArgument, "scene create handle is out of order");
            }
            ValidationSlot& slot = ValidationAt(impl, command.handle.index);
            if (impl.validationFreeHead != cyber::memory::kInvalidHandleIndex)
            {
                impl.validationFreeHead = slot.nextFree;
            }
            else
            {
                ++impl.validationSize;
                slot.generation = 1;
            }
            slot.nextFree = cyber::memory::kInvalidHandleIndex;
            slot.kind = command.kind;
            slot.occupied = true;
        }
        else
        {
            if (!command.handle.IsValid() || command.handle.index >= impl.validationSize)
            {
                return MakeError(ErrorCode::NotFound, "scene command handle does not exist");
            }
            ValidationSlot& slot = ValidationAt(impl, command.handle.index);
            if (!slot.occupied || slot.generation != command.handle.generation ||
                slot.kind != command.kind)
            {
                return MakeError(ErrorCode::NotFound,
                                 "scene command handle is stale or mismatched");
            }
            if (command.operation == kUpdateCommand)
            {
                bool valid = false;
                switch (command.kind)
                {
                case ProxyKind::MeshInstance:
                    valid = IsValid(command.mesh);
                    break;
                case ProxyKind::PointLight:
                    valid = IsValid(command.light);
                    break;
                case ProxyKind::Camera:
                    valid = IsValid(command.camera);
                    break;
                }
                if (!valid)
                {
                    return MakeError(ErrorCode::InvalidArgument, "invalid scene update command");
                }
            }
            else if (command.operation == kDestroyCommand)
            {
                slot.occupied = false;
                ++slot.generation;
                if (slot.generation == 0)
                {
                    slot.generation = 1;
                }
                slot.nextFree = impl.validationFreeHead;
                impl.validationFreeHead = command.handle.index;
            }
            else
            {
                return MakeError(ErrorCode::ParseError, "scene command has an unknown operation");
            }
        }
    }
    return Error{ErrorCode::Ok, nullptr};
}

void CullOne(void* userData, u32 index) noexcept
{
    auto& context = *static_cast<CullContext*>(userData);
    const SceneSlot& slot = context.impl->slots[index];
    CullSlot& result = context.output[index];
    result.visible = false;
    if (!slot.occupied || slot.proxy.kind == ProxyKind::Camera ||
        !cyber::math::Intersects(context.view->frustum, slot.proxy.worldBounds))
    {
        return;
    }
    result.handle = ProxyHandle{index, slot.generation};
    result.kind = slot.proxy.kind;
    result.lod = 0;
    if (slot.proxy.kind == ProxyKind::MeshInstance && slot.proxy.lodCount > 1)
    {
        const f32 radius = cyber::math::Length(slot.proxy.worldBounds.Extents());
        const f32 distance =
            cyber::math::Length(slot.proxy.worldBounds.Center() - context.view->cameraPosition);
        const f32 denominator = cyber::math::Max(distance - radius, context.view->nearDistance);
        const f32 projectedRadius = radius * context.view->projectionScalePixels / denominator;
        for (u32 lod = 0; lod + 1 < slot.proxy.lodCount; ++lod)
        {
            if (projectedRadius < slot.proxy.lodPixelThresholds[lod])
            {
                result.lod = lod + 1;
            }
        }
    }
    result.visible = true;
}

}  // namespace

Status<Error> Scene::ApplyCommands(Span<const Byte> commands) noexcept
{
    if (m_impl == nullptr)
    {
        return Status<Error>::Err(MakeError(ErrorCode::InvalidArgument, "scene is empty"));
    }
    if (commands.Size() % sizeof(SceneCommand) != 0)
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::ParseError, "scene command packet has a partial record"));
    }
    const usize count = commands.Size() / sizeof(SceneCommand);
    if (count > m_impl->maxCommands)
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::OutOfRange, "scene command packet exceeds configured capacity"));
    }
    if (count == 0)
    {
        return Status<Error>::Ok();
    }
    const Error validationError = ValidateCommandPacket(*m_impl, commands);
    if (validationError.code != ErrorCode::Ok)
    {
        return Status<Error>::Err(validationError);
    }
    for (usize index = 0; index < count; ++index)
    {
        SceneCommand command{};
        std::memcpy(&command, commands.Data() + index * sizeof(SceneCommand), sizeof(command));
        if (command.version != kSceneCommandVersion || !command.handle.IsValid())
        {
            return Status<Error>::Err(
                MakeError(ErrorCode::ParseError, "scene command version or handle is invalid"));
        }
        bool applied = false;
        if (command.operation == kCreateCommand)
        {
            applied = ApplyCreate(*m_impl, command);
        }
        else if (command.operation == kUpdateCommand)
        {
            applied = ApplyUpdate(*m_impl, command);
        }
        else if (command.operation == kDestroyCommand)
        {
            applied = ApplyDestroy(*m_impl, command);
        }
        if (!applied)
        {
            return Status<Error>::Err(
                MakeError(ErrorCode::Internal, "validated scene command could not be applied"));
        }
    }
    return Status<Error>::Ok();
}

Result<VisibilityList, Error> Scene::Cull(const SceneView& view, cyber::jobs::JobSystem& jobs,
                                          Allocator& resultAllocator) noexcept
{
    if (m_impl == nullptr)
    {
        return Result<VisibilityList, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene is empty"));
    }
    if (!jobs.IsRunning())
    {
        return Result<VisibilityList, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene culling requires a running job system"));
    }
    if (!IsValidView(view))
    {
        return Result<VisibilityList, Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene view contains invalid projection data"));
    }
    const usize slotCount = m_impl->slots.Size();
    if (slotCount > std::numeric_limits<u32>::max() || !m_impl->cullSlots.TryResize(slotCount))
    {
        return Result<VisibilityList, Error>::Err(
            MakeError(ErrorCode::Internal, "scene culling scratch allocation failed"));
    }

    VisibilityList visible{resultAllocator};
    if (m_impl->liveCount > std::numeric_limits<usize>::max() / sizeof(VisibleProxy))
    {
        return Result<VisibilityList, Error>::Err(
            MakeError(ErrorCode::Overflow, "scene visibility list size overflows"));
    }
    visible.m_capacity = m_impl->liveCount;
    if (visible.m_capacity != 0)
    {
        visible.m_entries = static_cast<VisibleProxy*>(resultAllocator.Allocate(
            visible.m_capacity * sizeof(VisibleProxy), alignof(VisibleProxy)));
        if (visible.m_entries == nullptr)
        {
            return Result<VisibilityList, Error>::Err(
                MakeError(ErrorCode::Internal, "scene visibility list allocation failed"));
        }
    }

    CullContext context{m_impl, &view, m_impl->cullSlots.Data()};
    if (slotCount != 0)
    {
        jobs.ParallelFor(static_cast<u32>(slotCount), 128, &CullOne, &context);
    }
    for (usize index = 0; index < slotCount; ++index)
    {
        const CullSlot& entry = m_impl->cullSlots[index];
        if (entry.visible)
        {
            std::construct_at(visible.m_entries + visible.m_size,
                              VisibleProxy{entry.handle, entry.kind, entry.lod});
            ++visible.m_size;
        }
    }
    return Result<VisibilityList, Error>::Ok(std::move(visible));
}

usize Scene::ProxyCount() const noexcept
{
    return m_impl == nullptr ? 0 : m_impl->liveCount;
}

u8 Scene::DirtyFlags(ProxyHandle handle) const noexcept
{
    if (m_impl == nullptr || !handle.IsValid() || handle.index >= m_impl->slots.Size())
    {
        return 0;
    }
    const SceneSlot& slot = m_impl->slots[handle.index];
    return slot.occupied && slot.generation == handle.generation ? slot.proxy.dirtyFlags : 0;
}

Status<Error> Scene::AcknowledgeDirty(ProxyHandle handle, u8 flags) noexcept
{
    if (m_impl == nullptr || !handle.IsValid() || handle.index >= m_impl->slots.Size())
    {
        return Status<Error>::Err(MakeError(ErrorCode::NotFound, "scene proxy handle is stale"));
    }
    SceneSlot& slot = m_impl->slots[handle.index];
    if (!slot.occupied || slot.generation != handle.generation)
    {
        return Status<Error>::Err(MakeError(ErrorCode::NotFound, "scene proxy handle is stale"));
    }
    constexpr u8 knownFlags = kProxyDirtyTransform | kProxyDirtyBounds;
    if ((flags & static_cast<u8>(~knownFlags)) != 0)
    {
        return Status<Error>::Err(
            MakeError(ErrorCode::InvalidArgument, "scene dirty flag mask is invalid"));
    }
    slot.proxy.dirtyFlags = static_cast<u8>(slot.proxy.dirtyFlags & static_cast<u8>(~flags));
    return Status<Error>::Ok();
}

void Scene::Destroy() noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    Allocator* allocator = m_impl->allocator;
    std::destroy_at(m_impl);
    allocator->Deallocate(m_impl);
    m_impl = nullptr;
}

}  // namespace cyber::scene
