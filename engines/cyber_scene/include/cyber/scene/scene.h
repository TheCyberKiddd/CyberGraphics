#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/jobs/job_system.h>
#include <cyber/math/aabb.h>
#include <cyber/math/frustum.h>
#include <cyber/math/mat4.h>
#include <cyber/math/vec3.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/handle.h>
#include <cyber/memory/span.h>

namespace cyber::scene
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;
using cyber::memory::Span;
using cyber::memory::usize;

struct ProxyTag;
using ProxyHandle = cyber::memory::Handle<ProxyTag>;

enum class ProxyKind : u32
{
    MeshInstance = 0,
    PointLight,
    Camera,
};

inline constexpr u8 kProxyDirtyTransform = 1U << 0U;
inline constexpr u8 kProxyDirtyBounds = 1U << 1U;
inline constexpr u32 kMaximumMeshLods = 4;
inline constexpr u32 kMaximumLodThresholds = kMaximumMeshLods - 1;
inline constexpr u32 kSceneCommandVersion = 1;

/// Renderer data copied into a mesh-instance proxy.
struct MeshInstanceDesc
{
    u64 meshId = 0;
    u64 materialId = 0;
    cyber::math::Mat4 transform{};
    cyber::math::Aabb localBounds{};
    u32 lodCount = 1;
    /// Strictly decreasing projected-radius thresholds, in pixels.
    cyber::core::f32 lodPixelThresholds[kMaximumLodThresholds]{};
};

/// Renderer data copied into a point-light proxy.
struct PointLightDesc
{
    cyber::math::Mat4 transform{};
    cyber::math::Vec3 color{1.0f, 1.0f, 1.0f};
    cyber::core::f32 intensity = 1.0f;
    cyber::core::f32 range = 1.0f;
};

/// Renderer data copied into a camera proxy.
struct CameraProxyDesc
{
    cyber::math::Mat4 transform{};
    cyber::math::Mat4 viewProjection{};
};

/// Preallocated limits for game-thread proxy command production.
struct SceneCommandWriterDesc
{
    u32 maxProxies = 0;
    u32 maxCommandsPerPacket = 0;
};

/// Preallocated limits for render-owner scene storage and command application.
struct SceneDesc
{
    u32 maxProxies = 0;
    u32 maxCommandsPerPacket = 0;
};

/// View inputs for frustum culling and screen-space LOD selection.
struct SceneView
{
    cyber::math::Frustum frustum{};
    cyber::math::Vec3 cameraPosition{};
    /// Viewport height divided by twice the tangent of half the vertical FOV.
    cyber::core::f32 projectionScalePixels = 1.0f;
    cyber::core::f32 nearDistance = 0.01f;
};

/// One visible renderer proxy and its selected mesh LOD.
struct VisibleProxy
{
    ProxyHandle handle{};
    ProxyKind kind = ProxyKind::MeshInstance;
    u32 lod = 0;
};

namespace detail
{

struct SceneImpl;
struct SceneCommandWriterImpl;

}  // namespace detail

/// Move-only ordered visibility output. Entries are stable until this value is
/// destroyed and appear in deterministic proxy-slot order.
class VisibilityList
{
public:
    /// Constructs an empty, unallocated list.
    VisibilityList() noexcept = default;
    /// Releases entries using the allocator supplied to `Scene::Cull`.
    ~VisibilityList();
    /// Visibility lists cannot be copied because they own allocator storage.
    VisibilityList(const VisibilityList&) = delete;
    /// Visibility lists cannot be copy-assigned because they own allocator storage.
    VisibilityList& operator=(const VisibilityList&) = delete;
    /// Transfers the owned entries and leaves `other` empty.
    /// @param other List whose storage is transferred.
    VisibilityList(VisibilityList&& other) noexcept;
    /// Releases this list and transfers storage from `other`.
    /// @param other List whose storage is transferred.
    /// @return This list.
    VisibilityList& operator=(VisibilityList&& other) noexcept;

    /// Returns the number of visible proxies.
    /// @return Entry count.
    [[nodiscard]] usize Size() const noexcept;
    /// Returns the visible entries, or null when empty.
    /// @return Non-owning pointer valid until this list is destroyed or moved.
    [[nodiscard]] const VisibleProxy* Data() const noexcept;
    /// Returns a non-owning view of visible entries.
    /// @return Entries valid until this list is destroyed or moved.
    [[nodiscard]] Span<const VisibleProxy> Entries() const noexcept;

private:
    friend class Scene;
    explicit VisibilityList(cyber::memory::Allocator& allocator) noexcept;
    void Destroy() noexcept;

    cyber::memory::Allocator* m_allocator = nullptr;
    VisibleProxy* m_entries = nullptr;
    usize m_size = 0;
    usize m_capacity = 0;
};

/// Game-thread producer of copied proxy operations for a render-thread packet.
class SceneCommandWriter
{
public:
    /// Constructs an empty writer that cannot accept commands.
    SceneCommandWriter() noexcept = default;
    /// Releases writer storage using its borrowed allocator.
    ~SceneCommandWriter();
    /// Writers cannot be copied because they own command and handle storage.
    SceneCommandWriter(const SceneCommandWriter&) = delete;
    /// Writers cannot be copy-assigned because they own command and handle storage.
    SceneCommandWriter& operator=(const SceneCommandWriter&) = delete;
    /// Transfers the writer and leaves `other` empty.
    /// @param other Writer whose storage is transferred.
    SceneCommandWriter(SceneCommandWriter&& other) noexcept;
    /// Releases this writer and transfers storage from `other`.
    /// @param other Writer whose storage is transferred.
    /// @return This writer.
    SceneCommandWriter& operator=(SceneCommandWriter&& other) noexcept;

    /// Creates bounded command and handle storage.
    /// @param desc Maximum live proxy handles and commands per packet.
    /// @param allocator Storage allocator, which must outlive the writer.
    /// @return A writer, or InvalidArgument/OutOfRange/Internal on invalid
    /// limits, overflow, or allocation failure.
    [[nodiscard]] static Result<SceneCommandWriter, Error>
    Create(const SceneCommandWriterDesc& desc, cyber::memory::Allocator& allocator) noexcept;

    /// Adds a copied mesh-instance creation command and returns its reserved handle.
    /// @param desc Mesh/material IDs, transform, local bounds, and LOD thresholds.
    /// @return A generation-checked handle, or an explicit validation/capacity error.
    [[nodiscard]] Result<ProxyHandle, Error>
    CreateMeshInstance(const MeshInstanceDesc& desc) noexcept;

    /// Adds a copied mesh-instance update command.
    /// @param handle Live mesh-instance handle returned by a create operation.
    /// @param desc Replacement renderer data.
    /// @return Success, or InvalidArgument/NotFound/Busy/OutOfRange.
    [[nodiscard]] Status<Error> UpdateMeshInstance(ProxyHandle handle,
                                                   const MeshInstanceDesc& desc) noexcept;

    /// Adds a copied point-light creation command.
    /// @param desc Transform, color, intensity, and range.
    /// @return A generation-checked handle, or an explicit validation/capacity error.
    [[nodiscard]] Result<ProxyHandle, Error> CreatePointLight(const PointLightDesc& desc) noexcept;

    /// Adds a copied point-light update command.
    /// @param handle Live point-light handle returned by a create operation.
    /// @param desc Replacement renderer data.
    /// @return Success, or InvalidArgument/NotFound/Busy/OutOfRange.
    [[nodiscard]] Status<Error> UpdatePointLight(ProxyHandle handle,
                                                 const PointLightDesc& desc) noexcept;

    /// Adds a copied camera creation command.
    /// @param desc Camera transform and view-projection matrix.
    /// @return A generation-checked handle, or an explicit validation/capacity error.
    [[nodiscard]] Result<ProxyHandle, Error> CreateCamera(const CameraProxyDesc& desc) noexcept;

    /// Adds a copied camera update command.
    /// @param handle Live camera handle returned by a create operation.
    /// @param desc Replacement renderer data.
    /// @return Success, or InvalidArgument/NotFound/Busy/OutOfRange.
    [[nodiscard]] Status<Error> UpdateCamera(ProxyHandle handle,
                                             const CameraProxyDesc& desc) noexcept;

    /// Adds a destroy operation and invalidates the producer-side handle.
    /// @param handle Live proxy handle.
    /// @return Success, or InvalidArgument/NotFound/Busy/OutOfRange.
    [[nodiscard]] Status<Error> Destroy(ProxyHandle handle) noexcept;

    /// Seals the current commands into an immutable byte view for
    /// `RenderThread::SubmitFrame`. Repeated calls return the same view until
    /// Commit; all writer mutations return Busy while sealed.
    /// @return Non-owning versioned command bytes valid until Commit or destruction.
    [[nodiscard]] Span<const cyber::memory::Byte> Seal() noexcept;

    /// Clears a sealed packet after the render-thread queue accepted it.
    /// @return Success, or InvalidArgument when no packet is sealed.
    [[nodiscard]] Status<Error> Commit() noexcept;

    /// Returns the maximum byte size required by any packet from this writer.
    /// @return Preallocated maximum packet size in bytes.
    [[nodiscard]] usize MaxPacketBytes() const noexcept;

    /// Returns the number of queued operations in the current packet.
    /// @return Number of operations, including create/update/destroy.
    [[nodiscard]] u32 CommandCount() const noexcept;

private:
    explicit SceneCommandWriter(detail::SceneCommandWriterImpl* impl) noexcept;
    void DestroyImpl() noexcept;
    detail::SceneCommandWriterImpl* m_impl = nullptr;
};

/// Render-owner scene containing copied renderer proxies and parallel culling.
class Scene
{
public:
    /// Constructs an empty scene that cannot accept packets or cull.
    Scene() noexcept = default;
    /// Releases scene and scratch storage using its borrowed allocator.
    ~Scene();
    /// Scenes cannot be copied because they own proxy storage.
    Scene(const Scene&) = delete;
    /// Scenes cannot be copy-assigned because they own proxy storage.
    Scene& operator=(const Scene&) = delete;
    /// Transfers the render-owner scene and leaves `other` empty.
    /// @param other Scene whose storage is transferred.
    Scene(Scene&& other) noexcept;
    /// Releases this scene and transfers storage from `other`.
    /// @param other Scene whose storage is transferred.
    /// @return This scene.
    Scene& operator=(Scene&& other) noexcept;

    /// Creates bounded contiguous proxy storage.
    /// @param desc Maximum live proxies and commands accepted per packet.
    /// @param allocator Storage allocator, which must outlive the scene.
    /// @return A scene, or InvalidArgument/OutOfRange/Internal on invalid
    /// limits, overflow, or allocation failure.
    [[nodiscard]] static Result<Scene, Error> Create(const SceneDesc& desc,
                                                     cyber::memory::Allocator& allocator) noexcept;

    /// Applies a sealed command packet in order. Call only on the scene's render
    /// owner, such as from the Phase 21 `RenderThread` frame callback.
    /// @param commands Immutable bytes produced by `SceneCommandWriter::Seal`.
    /// @return Success, or ParseError/InvalidArgument/NotFound/OutOfRange/Internal.
    [[nodiscard]] Status<Error> ApplyCommands(Span<const cyber::memory::Byte> commands) noexcept;

    /// Culls mesh and light proxies in parallel and chooses each visible mesh's LOD.
    /// @param view Frustum and projection values for this view.
    /// @param jobs Running job system used for independent per-slot tests.
    /// @param resultAllocator Allocator for the returned visibility list.
    /// @return Deterministically ordered entries, or validation/allocation errors.
    [[nodiscard]] Result<VisibilityList, Error>
    Cull(const SceneView& view, cyber::jobs::JobSystem& jobs,
         cyber::memory::Allocator& resultAllocator) noexcept;

    /// Returns the current number of live proxies.
    /// @return Live mesh, light, and camera proxy count.
    [[nodiscard]] usize ProxyCount() const noexcept;

    /// Returns dirty transform/bounds bits for a live handle.
    /// @param handle Generation-checked proxy handle.
    /// @return Dirty bits, or zero for a stale/invalid handle.
    [[nodiscard]] u8 DirtyFlags(ProxyHandle handle) const noexcept;

    /// Acknowledges dirty state after the renderer consumes its copied data.
    /// @param handle Generation-checked proxy handle.
    /// @param flags Subset of known dirty bits to clear.
    /// @return Success, or NotFound/InvalidArgument for a stale handle or mask.
    [[nodiscard]] Status<Error> AcknowledgeDirty(ProxyHandle handle, u8 flags) noexcept;

private:
    explicit Scene(detail::SceneImpl* impl) noexcept;
    void Destroy() noexcept;
    detail::SceneImpl* m_impl = nullptr;
};

}  // namespace cyber::scene
