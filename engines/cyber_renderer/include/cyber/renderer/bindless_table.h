#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/renderer/descriptor_allocator.h>
#include <cyber/rhi/device.h>
#include <cyber/rhi/frame_ring.h>

#include <array>

namespace cyber::renderer
{

using cyber::core::u32;
using cyber::core::u64;

/// Resource-binding route selected from reported device capabilities.
enum class ResourceBindingPath : cyber::core::u8
{
    /// Use a finite descriptor-array table and runtime shader indices.
    Bindless = 0,
    /// Use ordinary per-draw bind groups.
    Conventional,
};

/// Select bindless only when the device explicitly reports support.
///
/// @param device Device whose capabilities are queried.
/// @return Bindless when Feature::BindlessResources is present; Conventional otherwise.
[[nodiscard]] ResourceBindingPath SelectResourceBindingPath(const rhi::Device& device) noexcept;

/// Configuration for finite, frame-versioned bindless resource tables.
struct BindlessResourceTableDesc
{
    /// Number of texture and read-only buffer entries in each table.
    u32 capacity = 0;
    /// Number of frame-versioned groups, matching the frame ring.
    u32 framesInFlight = rhi::kDefaultFramesInFlight;
    /// Structured element stride required for every buffer in the table; zero
    /// denotes raw storage buffers.
    u32 storageElementStride = 0;
    /// Valid texture view placed in every unused texture slot.
    rhi::TextureViewHandle fallbackTexture{};
    /// Valid storage buffer placed in every unused buffer slot.
    rhi::BufferHandle fallbackBuffer{};
    /// Sampler bound to all table shaders.
    rhi::SamplerHandle sampler{};
};

/// A finite, frame-versioned table for sampled textures and read-only buffers.
///
/// Mutations change the desired table. `SyncFrame` publishes that table into
/// one group after the caller has acquired its safe frame slot. Resources
/// removed from the table remain caller-owned and must be retired only after
/// every older frame that could reference them has completed.
class BindlessResourceTable
{
public:
    /// Construct an empty table.
    BindlessResourceTable() noexcept = default;
    /// Destroy table groups and metadata; the device must still be alive.
    ~BindlessResourceTable();
    /// Tables are move-only.
    BindlessResourceTable(const BindlessResourceTable&) = delete;
    /// Tables are move-only.
    BindlessResourceTable& operator=(const BindlessResourceTable&) = delete;
    /// Transfer table ownership.
    BindlessResourceTable(BindlessResourceTable&& other) noexcept;
    /// Destroy this table and take another table's state.
    BindlessResourceTable& operator=(BindlessResourceTable&& other) noexcept;

    /// Create a bindless table when the device reports the required feature.
    ///
    /// @param device Device that owns the groups; borrowed for the table lifetime.
    /// @param allocator Metadata allocator; borrowed until Destroy.
    /// @param desc Fixed table shape and caller-owned fallbacks.
    /// @return Table, or Unsupported when bindless is unavailable, otherwise the
    ///         first validation, capacity, allocation, or RHI error.
    [[nodiscard]] static core::Result<BindlessResourceTable, core::Error>
    Create(rhi::Device& device, memory::Allocator& allocator,
           const BindlessResourceTableDesc& desc) noexcept;

    /// Allocate a texture index and populate it with a live view.
    /// @param view Caller-owned sampled texture view.
    /// @return Generation-counted shader index, or OutOfRange/InvalidArgument.
    [[nodiscard]] core::Result<PersistentDescriptor, core::Error>
    AllocateTexture(rhi::TextureViewHandle view) noexcept;
    /// Replace the view at a live texture index.
    /// @param descriptor Live texture descriptor handle.
    /// @param view New caller-owned sampled texture view.
    /// @return Ok, or InvalidArgument for a stale handle/view.
    [[nodiscard]] core::Status<core::Error> UpdateTexture(PersistentDescriptor descriptor,
                                                          rhi::TextureViewHandle view) noexcept;
    /// Free a texture index and restore its fallback view.
    /// @param descriptor Live texture descriptor handle.
    /// @return Ok, or InvalidArgument for a stale or double-freed handle.
    [[nodiscard]] core::Status<core::Error> FreeTexture(PersistentDescriptor descriptor) noexcept;

    /// Allocate a read-only buffer index and populate it with a live storage buffer.
    /// @param buffer Caller-owned storage buffer.
    /// @param offsetBytes Byte offset of this descriptor's range.
    /// @param sizeBytes Range size; zero means from offset to the buffer end.
    /// @return Generation-counted shader index, or OutOfRange/InvalidArgument.
    [[nodiscard]] core::Result<PersistentDescriptor, core::Error>
    AllocateBuffer(rhi::BufferHandle buffer, u64 offsetBytes = 0, u64 sizeBytes = 0) noexcept;
    /// Replace the buffer at a live index.
    /// @param descriptor Live buffer descriptor handle.
    /// @param buffer New caller-owned storage buffer.
    /// @param offsetBytes Byte offset of this descriptor's range.
    /// @param sizeBytes Range size; zero means from offset to the buffer end.
    /// @return Ok, or InvalidArgument for a stale handle/buffer.
    [[nodiscard]] core::Status<core::Error> UpdateBuffer(PersistentDescriptor descriptor,
                                                         rhi::BufferHandle buffer,
                                                         u64 offsetBytes = 0,
                                                         u64 sizeBytes = 0) noexcept;
    /// Free a buffer index and restore its fallback buffer.
    /// @param descriptor Live buffer descriptor handle.
    /// @return Ok, or InvalidArgument for a stale or double-freed handle.
    [[nodiscard]] core::Status<core::Error> FreeBuffer(PersistentDescriptor descriptor) noexcept;

    /// Publish desired resources into one fence-safe frame group.
    ///
    /// @param ring Frame ring that produced the frame token.
    /// @param frame Safe slot returned by its successful BeginFrame call.
    /// @return Ok when synchronized or already current, or InvalidArgument/RHI error.
    [[nodiscard]] core::Status<core::Error> SyncFrame(const rhi::FrameRing& ring,
                                                      const rhi::FrameSlot& frame) noexcept;
    /// Return the group to bind for one frame slot.
    /// @param slot Frame-slot index.
    /// @return Live group, or invalid handle for an out-of-range slot.
    [[nodiscard]] rhi::BindGroupHandle Group(u32 slot) const noexcept;
    /// Return the bind-group layout used by every frame group.
    /// @return Live layout, or invalid handle for an empty table.
    [[nodiscard]] rhi::BindGroupLayoutHandle Layout() const noexcept;
    /// Return table capacity per resource class.
    /// @return Capacity, or zero for an empty table.
    [[nodiscard]] u32 Capacity() const noexcept;
    /// Destroy RHI groups and release metadata.
    /// @return Ok, or the first destruction error; the object is reset regardless.
    [[nodiscard]] core::Status<core::Error> Destroy() noexcept;

private:
    void MoveFrom(BindlessResourceTable& other) noexcept;
    void Reset() noexcept;
    void AdvanceRevision() noexcept;
    [[nodiscard]] bool ValidTexture(rhi::TextureViewHandle view) const noexcept;
    [[nodiscard]] bool ValidBuffer(rhi::BufferHandle buffer, u64 offsetBytes,
                                   u64 sizeBytes) const noexcept;
    [[nodiscard]] core::Status<core::Error>
    FreeDesiredTexture(PersistentDescriptor descriptor) noexcept;
    [[nodiscard]] core::Status<core::Error>
    FreeDesiredBuffer(PersistentDescriptor descriptor) noexcept;

    rhi::Device* m_device = nullptr;
    memory::Allocator* m_allocator = nullptr;
    rhi::TextureViewHandle* m_textures = nullptr;
    rhi::BindingResource* m_buffers = nullptr;
    u32 m_capacity = 0;
    u32 m_framesInFlight = 0;
    u32 m_storageElementStride = 0;
    u64 m_revision = 1;
    rhi::TextureViewHandle m_fallbackTexture{};
    rhi::BufferHandle m_fallbackBuffer{};
    rhi::SamplerHandle m_sampler{};
    rhi::BindGroupLayoutHandle m_layout{};
    std::array<rhi::BindGroupHandle, rhi::kMaxFramesInFlight> m_groups{};
    std::array<u64, rhi::kMaxFramesInFlight> m_syncedRevisions{};
    PersistentDescriptorAllocator m_textureIndices;
    PersistentDescriptorAllocator m_bufferIndices;
};

}  // namespace cyber::renderer
