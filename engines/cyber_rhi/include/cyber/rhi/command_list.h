#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/span.h>
#include <cyber/memory/types.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/shader.h>
#include <cyber/rhi/texture.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::i32;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

struct DeviceInterface;

// Size of one index an indexed draw reads.
enum class IndexFormat : u8
{
    // 16-bit indices.
    Uint16 = 0,
    // 32-bit indices.
    Uint32,
    // Count of enumerators, for table tests. Not a format.
    Count
};

// Human-readable index format, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* IndexFormatName(IndexFormat format) noexcept
{
    switch (format)
    {
    case IndexFormat::Uint16:
        return "uint16";
    case IndexFormat::Uint32:
        return "uint32";
    case IndexFormat::Count:
        break;
    }
    return "unknown";
}

// Bytes one index of `format` occupies, or 0 for an invalid format.
[[nodiscard]] constexpr u32 IndexFormatBytes(IndexFormat format) noexcept
{
    switch (format)
    {
    case IndexFormat::Uint16:
        return 2;
    case IndexFormat::Uint32:
        return 4;
    case IndexFormat::Count:
        break;
    }
    return 0;
}

// The rectangle of a render target a draw may write, in pixels.
//
// Depth is in the 0..1 range and is not reversed by the viewport: the projection
// matrix already produces reverse-Z, so `minDepth` is the value at the near
// plane and `maxDepth` at the far plane, which is what D3D12 and Vulkan both
// expect.
struct Viewport
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float minDepth = 0.0f;
    float maxDepth = 1.0f;

    [[nodiscard]] static constexpr Viewport Of(float w, float h) noexcept
    {
        return Viewport{0.0f, 0.0f, w, h, 0.0f, 1.0f};
    }
};

// A scissor rectangle in pixels. A fragment outside it is discarded before the
// pixel shader runs, which is what every UI and debug drawing path relies on.
struct ScissorRect
{
    u32 x = 0;
    u32 y = 0;
    u32 width = 0;
    u32 height = 0;
};

// A non-indexed draw.
struct DrawArguments
{
    // Vertices to draw. Zero is invalid.
    u32 vertexCount = 0;
    // Instances to draw. At least one; zero is invalid.
    u32 instanceCount = 1;
    // First vertex read from every bound vertex buffer.
    u32 firstVertex = 0;
    // Instance number the first instance sees.
    u32 firstInstance = 0;
};

// An indexed draw.
struct DrawIndexedArguments
{
    // Indices to read. Zero is invalid.
    u32 indexCount = 0;
    // Instances to draw. At least one; zero is invalid.
    u32 instanceCount = 1;
    // First index read from the index buffer.
    u32 firstIndex = 0;
    // Signed value added to every index before the vertex is fetched, which is
    // how a mesh's submeshes share one vertex buffer.
    i32 vertexOffset = 0;
    // Instance number the first instance sees.
    u32 firstInstance = 0;
};

// A compute dispatch.
struct DispatchArguments
{
    // Work groups along each axis. Every axis must be at least one.
    u32 groupCountX = 1;
    u32 groupCountY = 1;
    u32 groupCountZ = 1;
};

// A copy between two buffers.
struct BufferCopy
{
    BufferHandle source;
    // Byte offset inside the source.
    u64 sourceOffset = 0;
    BufferHandle destination;
    // Byte offset inside the destination.
    u64 destinationOffset = 0;
    // Bytes to copy. Zero is invalid; both ranges must stay inside their
    // buffer; the ranges must not overlap, because an overlapping copy is
    // undefined in every API.
    u64 sizeBytes = 0;
};

// A copy between two textures. Both must have the same format and be in
// `CopySource` and `CopyDestination` respectively.
struct TextureCopy
{
    TextureHandle source;
    u32 sourceMipLevel = 0;
    u32 sourceArrayLayer = 0;
    TextureHandle destination;
    u32 destinationMipLevel = 0;
    u32 destinationArrayLayer = 0;
    // Extent copied from the origin of each side, in texels. Zero is invalid.
    u32 width = 0;
    u32 height = 1;
    u32 depth = 1;
};

// A copy between a buffer and a texture.
//
// The buffer layout is the tightest one every backend accepts without a
// per-row alignment requirement: `bufferRowPitch` is the distance between the
// start of two rows and must be at least the row's byte size, and
// `bufferDepthPitch` the distance between two slices.
struct BufferTextureCopy
{
    BufferHandle buffer;
    // Byte offset of the first texel in the buffer.
    u64 bufferOffset = 0;
    // Bytes between two rows of the image. Zero means tightly packed by the
    // format's block size.
    u32 bufferRowPitch = 0;
    // Bytes between two slices (3D textures and array layers). Zero means
    // `bufferRowPitch * rows`.
    u32 bufferDepthPitch = 0;
    TextureHandle texture;
    // Mip level and array layer the image region lives in.
    u32 mipLevel = 0;
    u32 arrayLayer = 0;
    // Origin of the region inside the subresource, in texels.
    u32 x = 0;
    u32 y = 0;
    u32 z = 0;
    // Extent of the region, in texels.
    u32 width = 0;
    u32 height = 1;
    u32 depth = 1;
};

// Description of a command list to create.
struct CommandListDesc
{
    // Queue class the list is recorded for. A list may only be submitted to a
    // queue of this class, because D3D12 command list types are not
    // interchangeable.
    QueueKind queue = QueueKind::Graphics;
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// Where a list is in its lifecycle.
enum class CommandListState : u8
{
    // Created, nothing recorded.
    Initial = 0,
    // Open for recording. Exactly one thread records at a time.
    Recording,
    // Finished and immutable. Only this state may be submitted.
    Ended,
    // Count of enumerators, for table tests. Not a state.
    Count
};

// Human-readable list state, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* CommandListStateName(CommandListState state) noexcept
{
    switch (state)
    {
    case CommandListState::Initial:
        return "initial";
    case CommandListState::Recording:
        return "recording";
    case CommandListState::Ended:
        return "ended";
    case CommandListState::Count:
        break;
    }
    return "unknown";
}

// What one list contains, counted as commands are accepted. A rejected command
// is not counted, so the numbers describe what a submission will actually do.
struct CommandListStats
{
    u32 renderPasses = 0;
    u32 draws = 0;
    u32 dispatches = 0;
    u32 copies = 0;
    u32 clears = 0;
    // Resources transitioned, not barrier calls.
    u32 barriers = 0;
    u32 pushConstants = 0;
    u32 pipelineChanges = 0;
    u32 bindGroupChanges = 0;
    // Debug labels opened.
    u32 debugLabels = 0;
};

// Per-list function table, handed out by `createCommandList` so a backend may
// use a different table per list type. Every entry is required: a null entry is
// a backend bug that list creation rejects.
//
// Every recording entry returns `ErrorCode::InvalidArgument` when the list is
// not recording or when the command breaks a state rule, and leaves the list
// unchanged in that case. A command that fails to allocate is
// `ErrorCode::Internal`.
struct CommandListInterface
{
    // Opens the list for recording from `Initial` or `Ended`, clearing any
    // previous contents. Recording while recording is `InvalidArgument`.
    Status<Error> (*begin)(void* internal) noexcept;
    // Closes the list. A pass or a debug label still open is `InvalidArgument`.
    Status<Error> (*end)(void* internal) noexcept;
    // Returns the list to `Initial` and forgets what it tracked. Safe in any
    // state, including while a pass is open.
    void (*reset)(void* internal) noexcept;
    // Begins a pass over `desc`'s attachments.
    Status<Error> (*beginRenderPass)(void* internal, const RenderPassDesc& desc) noexcept;
    // Ends the open pass and stores or discards its attachments.
    Status<Error> (*endRenderPass)(void* internal) noexcept;
    // Binds a complete pipeline. A graphics pipeline is required inside a pass
    // and a compute pipeline outside one.
    Status<Error> (*setPipeline)(void* internal, PipelineHandle pipeline) noexcept;
    // Binds a group at `slot`, which the pipeline layout must use.
    Status<Error> (*setBindGroup)(void* internal, BindGroupSlot slot,
                                  BindGroupHandle group) noexcept;
    // Binds a vertex buffer at a binding index the pipeline declares.
    Status<Error> (*setVertexBuffer)(void* internal, u32 binding, BufferHandle buffer,
                                     u64 offsetBytes) noexcept;
    // Binds an index buffer and its index format.
    Status<Error> (*setIndexBuffer)(void* internal, BufferHandle buffer, u64 offsetBytes,
                                    IndexFormat format) noexcept;
    // Sets the viewport of the open pass.
    Status<Error> (*setViewport)(void* internal, const Viewport& viewport) noexcept;
    // Sets the scissor rectangle of the open pass.
    Status<Error> (*setScissor)(void* internal, const ScissorRect& scissor) noexcept;
    // Draws vertices.
    Status<Error> (*draw)(void* internal, const DrawArguments& arguments) noexcept;
    // Draws indexed.
    Status<Error> (*drawIndexed)(void* internal, const DrawIndexedArguments& arguments) noexcept;
    // Draws with arguments in a buffer. Requires `Feature::IndirectDraw`.
    Status<Error> (*drawIndirect)(void* internal, BufferHandle arguments, u64 offsetBytes,
                                  u32 drawCount, u32 strideBytes) noexcept;
    // Draws indexed with arguments in a buffer. Requires `Feature::IndirectDraw`.
    Status<Error> (*drawIndexedIndirect)(void* internal, BufferHandle arguments, u64 offsetBytes,
                                         u32 drawCount, u32 strideBytes) noexcept;
    // Dispatches compute work groups.
    Status<Error> (*dispatch)(void* internal, const DispatchArguments& arguments) noexcept;
    // Dispatches with arguments in a buffer. Requires `Feature::Compute` and
    // `Feature::IndirectDraw`.
    Status<Error> (*dispatchIndirect)(void* internal, BufferHandle arguments,
                                      u64 offsetBytes) noexcept;
    // Copies between two buffers.
    Status<Error> (*copyBuffer)(void* internal, const BufferCopy& copy) noexcept;
    // Copies a buffer's bytes into a texture subresource.
    Status<Error> (*copyBufferToTexture)(void* internal, const BufferTextureCopy& copy) noexcept;
    // Copies a texture subresource into a buffer.
    Status<Error> (*copyTextureToBuffer)(void* internal, const BufferTextureCopy& copy) noexcept;
    // Copies between two textures.
    Status<Error> (*copyTexture)(void* internal, const TextureCopy& copy) noexcept;
    // Clears a color texture outside a pass. The texture must have
    // `TextureUsage::RenderTarget`.
    Status<Error> (*clearColorTexture)(void* internal, TextureHandle texture,
                                       const ClearColorValue& value,
                                       const SubresourceRange& range) noexcept;
    // Clears a depth or depth-stencil texture outside a pass. The texture must
    // have `TextureUsage::DepthStencil`.
    Status<Error> (*clearDepthStencilTexture)(void* internal, TextureHandle texture,
                                              const ClearDepthStencilValue& value,
                                              const SubresourceRange& range) noexcept;
    // Transitions resources in recording order.
    Status<Error> (*barrier)(void* internal, memory::Span<const BufferBarrier> buffers,
                             memory::Span<const TextureBarrier> textures) noexcept;
    // Writes push constants visible to `stages`. The range must be covered by
    // one range of the bound pipeline's layout.
    Status<Error> (*pushConstants)(void* internal, ShaderStageMask stages, u32 offsetBytes,
                                   memory::Span<const memory::Byte> data) noexcept;
    // Opens a named region that a GPU debugger shows as a group.
    Status<Error> (*beginDebugLabel)(void* internal, const char* label) noexcept;
    // Closes the innermost open label.
    Status<Error> (*endDebugLabel)(void* internal) noexcept;
    // The open pass, or a value with `valid` false when none is open. Never
    // fails, so a caller can query it to size a viewport.
    RenderPassInfo (*currentRenderPass)(const void* internal) noexcept;
    // Counters for what the list has accepted. Never fails.
    CommandListStats (*stats)(const void* internal) noexcept;
};

// What `createCommandList` hands back. Both members are required.
struct CommandListCreation
{
    // Per-list function table, with static lifetime.
    const CommandListInterface* interface = nullptr;
    // Backend-private list state, owned by the device's list table.
    void* internal = nullptr;
};

// A recorded list of GPU commands.
//
// A list is move-only and owns its slot: destroying it calls the device's
// destroy entry, so a list cannot be leaked by forgetting a call. A list must
// not outlive its device, exactly as a resource handle must not.
//
// Every recording method validates the list's own state first: a method called
// outside `Begin`/`End` returns `ErrorCode::InvalidArgument` and records
// nothing. Argument and resource-state rules are enforced by the backend and are
// documented on each method.
class CommandList
{
public:
    CommandList() noexcept = default;
    ~CommandList();

    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;

    CommandList(CommandList&& other) noexcept;
    CommandList& operator=(CommandList&& other) noexcept;

    // True when this list owns backend state.
    [[nodiscard]] bool IsValid() const noexcept;

    // Generation-counted identity, for submission and logging.
    [[nodiscard]] CommandListHandle Handle() const noexcept;

    // Where the list is in its lifecycle.
    [[nodiscard]] CommandListState State() const noexcept;

    // True while the list accepts recording calls.
    [[nodiscard]] bool IsRecording() const noexcept;

    // Queue class the list was created for.
    [[nodiscard]] QueueKind Queue() const noexcept;

    // Counters for what the list has accepted. Zeroed for an empty list.
    [[nodiscard]] CommandListStats Stats() const noexcept;

    // The open pass, or a value with `valid` false when no pass is open.
    [[nodiscard]] RenderPassInfo CurrentRenderPass() const noexcept;

    // Opnes the list for recording, discarding any previous contents. Returns
    // `ErrorCode::InvalidArgument` when the list is already recording, and
    // `ErrorCode::Internal` when the backend cannot reset its allocator.
    [[nodiscard]] Status<Error> Begin() noexcept;

    // Closes the list so it may be submitted. Returns `ErrorCode::InvalidArgument`
    // when the list is not recording, when a pass is still open, or when a debug
    // label was left open.
    [[nodiscard]] Status<Error> End() noexcept;

    // Discards the contents and returns the list to `Initial`. Always succeeds,
    // including while a pass is open, because a caller that abandons a frame must
    // not have to unwind it by hand.
    void Reset() noexcept;

    // Begins a pass over `desc`'s attachments. Returns `ErrorCode::InvalidArgument`
    // for a malformed description, for a pass already open, for an attachment
    // whose texture lacks render-target usage, for attachments that disagree on
    // extent or sample count, and for a view that appears twice.
    [[nodiscard]] Status<Error> BeginRenderPass(const RenderPassDesc& desc) noexcept;

    // Ends the open pass. Returns `ErrorCode::InvalidArgument` when no pass is
    // open.
    [[nodiscard]] Status<Error> EndRenderPass() noexcept;

    // Binds a pipeline. Returns `ErrorCode::InvalidArgument` for a stale handle,
    // for a graphics pipeline while no pass is open, for a compute pipeline while
    // one is, and for a pipeline whose attachment formats or sample count do not
    // match the open pass.
    [[nodiscard]] Status<Error> SetPipeline(PipelineHandle pipeline) noexcept;

    // Binds a group. Returns `ErrorCode::InvalidArgument` when no pipeline is
    // bound, when the pipeline layout does not use `slot`, or when the group's
    // layout is not the one the pipeline layout declares for the slot.
    [[nodiscard]] Status<Error> SetBindGroup(BindGroupSlot slot, BindGroupHandle group) noexcept;

    // Binds a vertex buffer at a binding the pipeline declares. Returns
    // `ErrorCode::InvalidArgument` when no graphics pipeline is bound, when the
    // binding index is out of range, when the buffer lacks vertex usage, or when
    // the buffer is not in the `VertexBuffer` state.
    [[nodiscard]] Status<Error> SetVertexBuffer(u32 binding, BufferHandle buffer,
                                                u64 offsetBytes = 0) noexcept;

    // Binds an index buffer. Returns `ErrorCode::InvalidArgument` when no
    // graphics pipeline is bound, when the buffer lacks index usage, when the
    // offset is not a multiple of the index size, or when the buffer is not in
    // the `IndexBuffer` state.
    [[nodiscard]] Status<Error> SetIndexBuffer(BufferHandle buffer, u64 offsetBytes = 0,
                                               IndexFormat format = IndexFormat::Uint32) noexcept;

    // Sets the viewport. Returns `ErrorCode::InvalidArgument` when no pass is
    // open, for a zero or non-finite extent, for a depth range outside 0..1, or
    // for a viewport that leaves the pass extent.
    [[nodiscard]] Status<Error> SetViewport(const Viewport& viewport) noexcept;

    // Sets the scissor. Returns `ErrorCode::InvalidArgument` when no pass is
    // open or for a rectangle that leaves the pass extent.
    [[nodiscard]] Status<Error> SetScissor(const ScissorRect& scissor) noexcept;

    // Draws `arguments.vertexCount` vertices. Returns `ErrorCode::InvalidArgument`
    // for a zero count, when no graphics pipeline is bound, when no pass is open,
    // when a vertex binding the pipeline declares is unbound, when a group slot
    // its layout uses is unbound, or when a bound resource is in the wrong state.
    [[nodiscard]] Status<Error> Draw(const DrawArguments& arguments) noexcept;

    // Draws indexed. Everything `Draw` requires plus a bound index buffer and an
    // index range that stays inside it.
    [[nodiscard]] Status<Error> DrawIndexed(const DrawIndexedArguments& arguments) noexcept;

    // Draws with arguments read from `arguments`, a buffer with indirect usage
    // in the `IndirectArgument` state. `strideBytes` is the size of one argument
    // structure and must be at least 16.
    [[nodiscard]] Status<Error> DrawIndirect(BufferHandle arguments, u64 offsetBytes, u32 drawCount,
                                             u32 strideBytes = 16) noexcept;

    // Draws indexed with arguments read from a buffer. `strideBytes` must be at
    // least 20.
    [[nodiscard]] Status<Error> DrawIndexedIndirect(BufferHandle arguments, u64 offsetBytes,
                                                    u32 drawCount, u32 strideBytes = 20) noexcept;

    // Dispatches compute work. Returns `ErrorCode::Unsupported` when
    // `Feature::Compute` is absent, and `ErrorCode::InvalidArgument` when no
    // compute pipeline is bound, when a pass is open, or when a group slot the
    // layout uses is unbound.
    [[nodiscard]] Status<Error> Dispatch(const DispatchArguments& arguments) noexcept;

    // Dispatches with arguments read from a buffer.
    [[nodiscard]] Status<Error> DispatchIndirect(BufferHandle arguments, u64 offsetBytes) noexcept;

    // Copies between buffers.
    [[nodiscard]] Status<Error> CopyBuffer(const BufferCopy& copy) noexcept;

    // Copies bytes into a texture subresource.
    [[nodiscard]] Status<Error> CopyBufferToTexture(const BufferTextureCopy& copy) noexcept;

    // Copies a texture subresource into bytes.
    [[nodiscard]] Status<Error> CopyTextureToBuffer(const BufferTextureCopy& copy) noexcept;

    // Copies between two textures.
    [[nodiscard]] Status<Error> CopyTexture(const TextureCopy& copy) noexcept;

    // Clears a color texture outside a pass. `range` selects the subresources;
    // zero counts mean every remaining one.
    [[nodiscard]] Status<Error>
    ClearColorTexture(TextureHandle texture, const ClearColorValue& value,
                      const SubresourceRange& range = SubresourceRange{}) noexcept;

    // Clears a depth or depth-stencil texture outside a pass.
    [[nodiscard]] Status<Error>
    ClearDepthStencilTexture(TextureHandle texture, const ClearDepthStencilValue& value,
                             const SubresourceRange& range = SubresourceRange{}) noexcept;

    // Transitions resources. Every barrier is validated against the list's view
    // of state before any is applied, so one bad barrier changes nothing.
    [[nodiscard]] Status<Error> Barrier(memory::Span<const BufferBarrier> buffers,
                                        memory::Span<const TextureBarrier> textures = {}) noexcept;

    // Writes push constants. `offsetBytes` is relative to the block, `stages`
    // names the stages that read it, and the range must be covered by exactly
    // one range of the bound pipeline's layout.
    [[nodiscard]] Status<Error> PushConstants(ShaderStageMask stages, u32 offsetBytes,
                                              memory::Span<const memory::Byte> data) noexcept;

    // Opens a named debug region. Never fails on a backend without
    // `Feature::DebugMarkers`; the label is simply not emitted.
    [[nodiscard]] Status<Error> BeginDebugLabel(const char* label) noexcept;

    // Closes the innermost debug region.
    [[nodiscard]] Status<Error> EndDebugLabel() noexcept;

private:
    friend class Device;

    void Release() noexcept;
    void MoveFrom(CommandList& other) noexcept;

    // Device the list belongs to, for destruction. Null for an empty list.
    const DeviceInterface* m_device = nullptr;
    void* m_deviceInternal = nullptr;
    const CommandListInterface* m_interface = nullptr;
    void* m_internal = nullptr;
    CommandListHandle m_handle;
    CommandListState m_state = CommandListState::Initial;
    QueueKind m_queue = QueueKind::Graphics;
};

// Checks the parts of a draw call that need no access to bound resources: a
// non-zero vertex count and a non-zero instance count.
[[nodiscard]] Status<Error> ValidateDrawArguments(const DrawArguments& arguments) noexcept;

// Checks an indexed draw: non-zero index and instance counts.
[[nodiscard]] Status<Error>
ValidateDrawIndexedArguments(const DrawIndexedArguments& arguments) noexcept;

// Checks a dispatch: every axis at least one, and the total below the device
// limit is the backend's check.
[[nodiscard]] Status<Error> ValidateDispatchArguments(const DispatchArguments& arguments) noexcept;

// Checks a viewport: finite, positive extent, depth range inside 0..1 and not
// inverted.
[[nodiscard]] Status<Error> ValidateViewport(const Viewport& viewport) noexcept;

// Checks a scissor: non-zero extent.
[[nodiscard]] Status<Error> ValidateScissor(const ScissorRect& scissor) noexcept;

// Checks a buffer copy without the buffer sizes: non-zero size, and no overlap
// when source and destination are the same buffer.
[[nodiscard]] Status<Error> ValidateBufferCopy(const BufferCopy& copy) noexcept;

// Checks a texture copy without the texture shapes: non-zero extent.
[[nodiscard]] Status<Error> ValidateTextureCopy(const TextureCopy& copy) noexcept;

// Checks a buffer-texture copy without the resource shapes: non-zero extent, a
// row pitch large enough for the row, and a depth pitch that covers its rows.
// `bytesPerRow` is the tight row size the caller computes from the format, or 0
// when the backend should not check it.
[[nodiscard]] Status<Error> ValidateBufferTextureCopy(const BufferTextureCopy& copy,
                                                      u32 bytesPerRow) noexcept;

// Checks a buffer-texture copy against the bytes a stride needs: `elementBytes`
// is the format's block size and `blockPixels` its block width in texels.
[[nodiscard]] Status<Error> ValidateIndirectArguments(u64 offsetBytes, u32 count, u32 strideBytes,
                                                      u32 minimumStrideBytes) noexcept;

// Size of one indirect draw argument structure as the RHI defines it: an
// argument buffer is `DrawArguments` for `DrawIndirect` and
// `DrawIndexedArguments` for `DrawIndexedIndirect`.
inline constexpr u32 kIndirectDrawStrideBytes = 16;
inline constexpr u32 kIndirectDrawIndexedStrideBytes = 20;
inline constexpr u32 kIndirectDispatchStrideBytes = 12;

// Bytes one `DrawArguments` occupies in an indirect buffer: four 32-bit fields.
inline constexpr u32 kIndirectDispatchArgumentBytes = 12;

// Largest group count along one axis a dispatch may ask for. Every backend this
// project targets accepts at least this.
inline constexpr u32 kMaxDispatchGroupsPerAxis = 65535;

}  // namespace cyber::rhi
