#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/backend.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/descriptors.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/shader.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/swapchain.h>
#include <cyber/rhi/texture.h>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u64;

// The GPU device: the owner of every RHI object and the only way to create or
// destroy one. A device is a value that owns a backend's per-device state, not a
// handle, because it owns the table that resolves handles.
//
// Lifetime rules (ADR 0013):
//
// - The device owns every object created from it. Destroying the device waits
//   for submitted work and then releases what remains; there is no per-object
//   reference count.
// - Destroying an object invalidates its handle. A stale handle is reported as
//   `ErrorCode::InvalidArgument`, never resolved into a newer object.
// - A view borrows its texture. Destroying the texture retires every view of it,
//   so a view handle never outlives the resource it reads.
// - Handles must not outlive their device. Using one after the device is gone is
//   a caller bug the type system cannot catch; in debug builds the handle fails
//   to resolve rather than corrupting memory, because nothing is left to
//   resolve it.
// - A moved-from device is empty: it has no capabilities, returns invalid queue
//   handles, and rejects every call.
//
// Threading rules: `Capabilities`, `Queue`, fence create/signal/wait/value,
// `Submit`, and every resource create, destroy, write, read, query, and
// transition are safe to call from any thread. The device does its own locking;
// a backend whose native API needs external synchronization implements it
// internally. Recording a command list is not safe from two threads and does not
// need to be: each recording thread owns its own list, which is the model Phase
// 21 builds on.
class Device
{
public:
    Device() noexcept = default;
    ~Device();

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    Device(Device&& other) noexcept;
    Device& operator=(Device&& other) noexcept;

    // Creates a device on `desc.backend` with `allocator` backing any
    // bookkeeping the backend keeps. Returns:
    // - `ErrorCode::InvalidArgument` when the backend is null, its tables are
    //   incomplete, or `desc.adapterIndex` names an adapter the backend cannot
    //   enumerate.
    // - `ErrorCode::Unsupported` when the backend exists but cannot create a
    //   device on this adapter.
    // - `ErrorCode::Internal` when allocation, backend setup, or the per-device
    //   function table is incomplete.
    // The returned device owns backend state; nothing is shared with another
    // device.
    [[nodiscard]] static Result<Device, Error> Create(const DeviceDesc& desc,
                                                      memory::Allocator& allocator) noexcept;

    // True when this device owns backend state.
    [[nodiscard]] bool IsValid() const noexcept;

    // Name of the backend that created the device, or "none" for an empty one.
    [[nodiscard]] const char* BackendName() const noexcept;

    // Guarantees of this device. A value type; safe to call from any thread and
    // safe to keep. An empty device reports zeroed limits and no features.
    [[nodiscard]] Capabilities Capabilities() const noexcept;

    // Handle of the queue for `kind`, or an invalid handle when this device has
    // no such queue. Queue handles live as long as the device.
    [[nodiscard]] QueueHandle Queue(QueueKind kind) const noexcept;

    // Creates a fence whose timeline starts at `desc.initialValue`.
    // `ErrorCode::InvalidArgument` for an empty device; `ErrorCode::Internal`
    // when the backend cannot allocate the object.
    [[nodiscard]] Result<FenceHandle, Error> CreateFence(const FenceDesc& desc) noexcept;

    // Destroys a fence and invalidates its handle. A stale or invalid handle is
    // `ErrorCode::InvalidArgument`.
    [[nodiscard]] Status<Error> DestroyFence(FenceHandle fence) noexcept;

    // Advances `fence` to `value`. A value at or below the current one is
    // accepted and ignored, because the timeline never moves backwards; a stale
    // handle is `ErrorCode::InvalidArgument`.
    [[nodiscard]] Status<Error> SignalFence(FenceHandle fence, u64 value) noexcept;

    // Waits until `fence` reaches at least `value`. `timeoutNanoseconds` is
    // `0` to poll and `kInfiniteWaitNanoseconds` to block. Returns
    // `ErrorCode::NotFound` when the timeout expires first, which a caller must
    // treat separately from the `ErrorCode::InvalidArgument` a stale handle
    // produces.
    [[nodiscard]] Status<Error>
    WaitFence(FenceHandle fence, u64 value,
              u64 timeoutNanoseconds = kInfiniteWaitNanoseconds) noexcept;

    // Current value of `fence`, or 0 for a stale handle.
    [[nodiscard]] u64 FenceValue(FenceHandle fence) const noexcept;

    // Creates a buffer from `desc`. Returns `ErrorCode::InvalidArgument` for a
    // descriptor no backend can create (no usage, zero size, host-visible
    // combined with a shader usage, a stride that is not a multiple of 4),
    // `ErrorCode::OutOfRange` for a size or stride above the device limits, and
    // `ErrorCode::Internal` when the allocation fails.
    [[nodiscard]] Result<BufferHandle, Error> CreateBuffer(const BufferDesc& desc) noexcept;

    // Destroys a buffer and invalidates its handle. A stale handle is
    // `ErrorCode::InvalidArgument`. The storage is released immediately, so a
    // caller must not destroy a buffer a submission still reads; the retirement
    // queue in `retirement.h` exists to hold it until the timeline passes.
    [[nodiscard]] Status<Error> DestroyBuffer(BufferHandle buffer) noexcept;

    // Size, stride, and usage of a live buffer. A stale handle returns a value
    // with `valid` false rather than an error, so a query never has to be
    // wrapped in a `Result`.
    [[nodiscard]] BufferInfo BufferInfoOf(BufferHandle buffer) const noexcept;

    // Copies `bytes` into a buffer created with `BufferUsage::HostVisible`.
    // Returns `ErrorCode::InvalidArgument` for a stale handle, a buffer without
    // that usage, or a null span with a non-zero size; `ErrorCode::OutOfRange`
    // when `offset + bytes.Size()` overflows or exceeds the buffer.
    [[nodiscard]] Status<Error> WriteBuffer(BufferHandle buffer, u64 offset,
                                            memory::Span<const memory::Byte> bytes) noexcept;

    // Copies `bytes` out of a buffer the CPU can map: one created with
    // `BufferUsage::Readback` or `BufferUsage::HostVisible`. A device-local
    // buffer is `ErrorCode::InvalidArgument`; the range errors are the ones
    // `WriteBuffer` documents.
    [[nodiscard]] Status<Error> ReadBuffer(BufferHandle buffer, u64 offset,
                                           memory::Span<memory::Byte> bytes) noexcept;

    // State of a whole buffer, or `ResourceState::Undefined` for a stale handle.
    [[nodiscard]] ResourceState BufferStateOf(BufferHandle buffer) const noexcept;

    // Creates a texture from `desc`. Returns `ErrorCode::InvalidArgument` for a
    // malformed shape (zero extent, cube that is not square, 3D with layers or
    // multisampling, a mip count above the chain, a usage its format cannot
    // serve), `ErrorCode::OutOfRange` above the device limits, and
    // `ErrorCode::Unsupported` for a usage whose feature the adapter lacks.
    [[nodiscard]] Result<TextureHandle, Error> CreateTexture(const TextureDesc& desc) noexcept;

    // Destroys a texture, invalidates its handle, and retires every view of it.
    // A stale handle is `ErrorCode::InvalidArgument`.
    [[nodiscard]] Status<Error> DestroyTexture(TextureHandle texture) noexcept;

    // Shape and size of a live texture. A stale handle returns a value with
    // `valid` false.
    [[nodiscard]] TextureInfo TextureInfoOf(TextureHandle texture) const noexcept;

    // Creates a view over a live texture. Returns `ErrorCode::InvalidArgument`
    // for a stale texture, a format that is not view-compatible, a shape the
    // texture cannot expose, or a range outside the texture.
    [[nodiscard]] Result<TextureViewHandle, Error>
    CreateTextureView(const TextureViewDesc& desc) noexcept;

    // Destroys one view. The texture stays alive and usable. A stale handle is
    // `ErrorCode::InvalidArgument`.
    [[nodiscard]] Status<Error> DestroyTextureView(TextureViewHandle view) noexcept;

    // Format, shape, and range of a live view. A stale handle returns a value
    // with `valid` false.
    [[nodiscard]] TextureViewInfo TextureViewInfoOf(TextureViewHandle view) const noexcept;

    // State of one `(mip, array layer)` subresource. Returns
    // `ResourceState::Undefined` when the texture handle or either index is
    // invalid.
    [[nodiscard]] ResourceState TextureStateOf(TextureHandle texture, u32 mipLevel,
                                               u32 arrayLayer) const noexcept;

    // Creates a sampler from `desc`. Returns `ErrorCode::InvalidArgument` for a
    // non-finite LOD, `minLod` above `maxLod`, an anisotropy outside
    // `1..Limits::maxSamplerAnisotropy`, or anisotropy combined with a nearest
    // filter; `ErrorCode::Unsupported` when anisotropy is requested and
    // `Feature::AnisotropicFiltering` is absent.
    [[nodiscard]] Result<SamplerHandle, Error> CreateSampler(const SamplerDesc& desc) noexcept;

    // Destroys a sampler and invalidates its handle.
    [[nodiscard]] Status<Error> DestroySampler(SamplerHandle sampler) noexcept;

    // Creates a shader module. Returns `ErrorCode::InvalidArgument` for an
    // unknown stage or bytecode format, an empty code blob, or a source format
    // with no entry point.
    [[nodiscard]] Result<ShaderHandle, Error> CreateShader(const ShaderDesc& desc) noexcept;

    // Destroys a shader module. Pipelines already created from it stay valid.
    [[nodiscard]] Status<Error> DestroyShader(ShaderHandle shader) noexcept;

    // Stage, format, and code size of a live shader module. A stale handle
    // returns a value with `valid` false.
    [[nodiscard]] ShaderInfo ShaderInfoOf(ShaderHandle shader) const noexcept;

    // Creates a bind group layout. Returns `ErrorCode::InvalidArgument` for an
    // empty layout, a duplicate or out-of-range binding, an unknown type, an
    // empty visibility mask, or a zero array count.
    [[nodiscard]] Result<BindGroupLayoutHandle, Error>
    CreateBindGroupLayout(const BindGroupLayoutDesc& desc) noexcept;

    // Destroys a bind group layout. Groups created from it copy what they need
    // and stay valid.
    [[nodiscard]] Status<Error> DestroyBindGroupLayout(BindGroupLayoutHandle layout) noexcept;

    // Creates a bind group from a live layout and resources that match it
    // position for position. Returns `ErrorCode::InvalidArgument` for a stale
    // layout, a resource count mismatch, a type mismatch, a range outside its
    // buffer, a misaligned range, a resource whose usage does not fit the
    // binding, or a stale resource handle.
    [[nodiscard]] Result<BindGroupHandle, Error>
    CreateBindGroup(const BindGroupDesc& desc) noexcept;

    // Destroys a bind group.
    [[nodiscard]] Status<Error> DestroyBindGroup(BindGroupHandle group) noexcept;

    // Layout and resource count of a live bind group. A stale handle returns a
    // value with `valid` false.
    [[nodiscard]] BindGroupInfo BindGroupInfoOf(BindGroupHandle group) const noexcept;

    // Creates a pipeline layout. Returns `ErrorCode::InvalidArgument` for no
    // used slot, a stale group layout, a duplicate group layout, a push-constant
    // range that overflows or overlaps another for a shared stage, or more than
    // `kMaxPushConstantRanges` ranges.
    [[nodiscard]] Result<PipelineLayoutHandle, Error>
    CreatePipelineLayout(const PipelineLayoutDesc& desc) noexcept;

    // Destroys a pipeline layout. Pipelines created from it stay valid.
    [[nodiscard]] Status<Error> DestroyPipelineLayout(PipelineLayoutHandle layout) noexcept;

    // Creates a graphics pipeline. Every state rule is validated and reported
    // with `ErrorCode::InvalidArgument`; a description the adapter cannot serve
    // reports `ErrorCode::Unsupported`. The optional cache is consulted but does
    // not change the identity of the returned handle.
    [[nodiscard]] Result<PipelineHandle, Error>
    CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) noexcept;

    // Destroys a graphics pipeline.
    [[nodiscard]] Status<Error> DestroyGraphicsPipeline(PipelineHandle pipeline) noexcept;

    // Creates a compute pipeline. `ErrorCode::Unsupported` when
    // `Feature::Compute` is absent.
    [[nodiscard]] Result<PipelineHandle, Error>
    CreateComputePipeline(const ComputePipelineDesc& desc) noexcept;

    // Destroys a compute pipeline.
    [[nodiscard]] Status<Error> DestroyComputePipeline(PipelineHandle pipeline) noexcept;

    // Creates a pipeline cache, optionally seeded with bytes a previous run
    // saved through `PipelineCacheBytes`.
    [[nodiscard]] Result<PipelineCacheHandle, Error>
    CreatePipelineCache(const PipelineCacheDesc& desc) noexcept;

    // Destroys a pipeline cache. Pipelines created with it stay valid.
    [[nodiscard]] Status<Error> DestroyPipelineCache(PipelineCacheHandle cache) noexcept;

    // Returns the cache's saved bytes, allocated from `allocator`. A backend
    // with nothing to save returns an empty array, which is a valid result, not
    // an error. This is a diagnostic and persistence path: it may stall a GPU
    // backend and must not be called per frame.
    [[nodiscard]] Result<memory::DynamicArray<memory::Byte>, Error>
    PipelineCacheBytes(PipelineCacheHandle cache, memory::Allocator& allocator) const noexcept;

    // Kind and description hash of a live pipeline. A stale handle returns a
    // value with `valid` false.
    [[nodiscard]] PipelineInfo PipelineInfoOf(PipelineHandle pipeline) const noexcept;

    // Transitions every barrier in both spans and records the new state. Every
    // barrier is validated before any is applied: a batch with one bad barrier
    // changes nothing and reports `ErrorCode::InvalidArgument`, so a caller can
    // fix the descriptor and resubmit. `Undefined` on a `from` side discards
    // contents; `Undefined` on a `to` side is rejected.
    [[nodiscard]] Status<Error> Transition(memory::Span<const BufferBarrier> buffers,
                                           memory::Span<const TextureBarrier> textures) noexcept;

    // Blocks until the device has finished all submitted work, and is the only
    // point at which a caller may destroy an object a submission used without
    // retiring it first. Returns `ErrorCode::InvalidArgument` for an empty
    // device.
    [[nodiscard]] Status<Error> WaitIdle() noexcept;

    // Creates a command list recorded for `desc.queue`. The returned list owns
    // its slot: destroying it releases the slot, so there is no destroy call to
    // forget and no way to destroy a list twice.
    //
    // Returns `ErrorCode::InvalidArgument` for an empty device, for a queue
    // class this device has no queue for, and for a backend whose command list
    // table is incomplete; `ErrorCode::Internal` when the backend cannot create
    // the list.
    [[nodiscard]] Result<CommandList, Error>
    CreateCommandList(const CommandListDesc& desc) noexcept;

    // Runs a batch of recorded lists on one queue.
    //
    // The lists run in order; the queue first waits for every GPU-side wait to be
    // satisfied, then reaches every signal when the last list has finished. The
    // call itself does not wait for the GPU, so a caller records the next frame
    // while this one runs, bounded by its frame ring.
    //
    // Returns `ErrorCode::InvalidArgument` for a malformed batch, a stale list
    // handle, a list that has not ended, or a list recorded for another queue
    // class; `ErrorCode::Unsupported` when the backend cannot signal or wait on
    // fences at all; `ErrorCode::Internal` when the backend cannot submit.
    [[nodiscard]] Status<Error> Submit(const SubmitDesc& desc) noexcept;

    // What the last submission on `kind` did. A queue the device does not have,
    // or a device with no submissions yet, reports a value with `valid` false.
    [[nodiscard]] SubmissionInfo LastSubmission(QueueKind kind) const noexcept;

    // Creates a swapchain over a surface the caller owns.
    //
    // The surface must outlive the swapchain, and the swapchain owns its images:
    // `SwapChain::Resize` replaces them and invalidates every handle a caller
    // kept. Returns `ErrorCode::InvalidArgument` for a malformed description or
    // an empty device, `ErrorCode::Unsupported` for a present mode the adapter
    // cannot serve, and `ErrorCode::OutOfRange` for an extent above the device
    // limit.
    [[nodiscard]] Result<SwapChain, Error> CreateSwapChain(const SwapChainDesc& desc) noexcept;

private:
    void Release() noexcept;
    void MoveFrom(Device& other) noexcept;

    const Backend* m_backend = nullptr;
    const DeviceInterface* m_interface = nullptr;
    void* m_internal = nullptr;
};

}  // namespace cyber::rhi
