// The validating device (ADR 0018): see `validating_device.h` for the shape.
//
// Every entry forwards to the backend whose device state this wrapper holds.
// The order of work in each entry is the same: resolve the wrapper's own
// state, run the shared rules from `validation.h` and the registry checks
// those rules cannot express, then make the real call. A check the null
// backend's recorded state machine owns — what a pass binds, which state a
// subresource is in — is not re-implemented here: `Standard` never tracks
// subresource state, and the backend's own answer to such a call is forwarded
// unchanged.
//
// The wrapper is transparent about handles: a create returns the backend's own
// handle, unchanged, so the caller cannot tell wrapper-created resources from
// backend-internal ones such as swapchain images. What the wrapper tracks
// instead are two things the shared rules need a registry for:
//
// - what each live command list has named since its last `Begin`, so a submit
//   can stamp every resource the work touches, and
// - which fence values the stamped resources are held by, so a destroy of one
//   whose fence is still unreached is caught before the backend frees storage
//   a submitted submission is still reading.
//
// Registries are keyed by the backend's own handles: the wrapper sees every
// create and destroy of the kinds it tracks, so a handle's table lifetime
// mirrors the backend's exactly. A handle the table does not know (a resource
// created inside the backend) is forwarded untouched, which the header calls
// out as the transparent answer.

#include "validating_device.h"

#include "validation.h"

#include <cyber/core/assert.h>
#include <cyber/core/error.h>
#include <cyber/core/log.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/handle.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/binding.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/capabilities.h>
#include <cyber/rhi/command_list.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/memory.h>
#include <cyber/rhi/pipeline.h>
#include <cyber/rhi/render_pass.h>
#include <cyber/rhi/resource_state.h>
#include <cyber/rhi/sampler.h>
#include <cyber/rhi/shader.h>
#include <cyber/rhi/submission.h>
#include <cyber/rhi/swapchain.h>
#include <cyber/rhi/texture.h>

#include <cstdint>
#include <mutex>
#include <new>
#include <utility>

namespace cyber::rhi
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Status;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::DynamicArray;
using cyber::memory::Span;

namespace
{

// Distinct resources one list may name between two begins. A record beyond
// this stops growing the set and logs once: an untracked touch can only make
// the in-flight check quieter, so the bound never makes a valid call fail.
constexpr u32 kMaxTouchesPerSet = 256;

// Distinct resources one submission may stamp. Submission is on the frame path
// and the stamp set is fixed size; going over is logged once and can only make
// the check quieter.
constexpr u32 kMaxStampsPerSubmit = 512;

// Fence holds one resource remembers. A guard whose fence has reached its
// value reads as inert at destroy time, so the bound needs no pruning on the
// happy path, and going past it makes the check quieter, never a valid call
// fail.
constexpr u32 kMaxGuardsPerResource = 4;

// One resource a set names: either a buffer or a texture.
struct Touch
{
    // True when `texture` names the resource, false when `buffer` does.
    bool isTexture = false;
    BufferHandle buffer{};
    TextureHandle texture{};

    // True when the touch names a resource. Empty keys are skipped so a caller
    // that builds touches from optional fields does not have to filter.
    [[nodiscard]] bool NamesResource() const noexcept
    {
        return isTexture ? texture.IsValid() : buffer.IsValid();
    }
};

[[nodiscard]] bool operator==(const Touch& a, const Touch& b) noexcept
{
    return a.isTexture == b.isTexture && a.buffer == b.buffer && a.texture == b.texture;
}

[[nodiscard]] Touch TouchOfBuffer(BufferHandle buffer) noexcept
{
    Touch touch;
    touch.isTexture = false;
    touch.buffer = buffer;
    touch.texture = TextureHandle{};
    return touch;
}

[[nodiscard]] Touch TouchOfTexture(TextureHandle texture) noexcept
{
    Touch touch;
    touch.isTexture = true;
    touch.buffer = BufferHandle{};
    touch.texture = texture;
    return touch;
}

// Adds `touch` to a bounded, distinct set. Full is quieter, never stricter, and
// is a trace-level note rather than an error.
template <u32 kCapacity>
void AddTouch(Touch (&entries)[kCapacity], u32& count, const Touch& touch) noexcept
{
    static_assert(kCapacity <= u32{1024}, "touch sets are fixed buffers, not arenas");
    if (!touch.NamesResource())
    {
        return;
    }
    for (u32 index = 0; index < count; ++index)
    {
        if (entries[index] == touch)
        {
            return;
        }
    }
    if (count < kCapacity)
    {
        entries[count] = touch;
        ++count;
        return;
    }
    CG_LOG(Trace, Rhi, "validating device: touch set full, a resource touch is untracked");
}

// A fence value from a submission that named the resource the guard belongs
// to. An invalid handle guards nothing, and a guard whose fence has reached
// its value no longer blocks a destroy.
struct Guard
{
    FenceHandle fence{};
    u64 value = 0;

    [[nodiscard]] bool Holds() const noexcept
    {
        return fence.IsValid();
    }

    // True when the guard no longer blocks a destroy. The probe is a
    // zero-timeout wait rather than a fence value: `FenceValue` reports the
    // highest value a submission has *queued*, which both backends set at
    // submit time, so it cannot tell "queued" from "run". `WaitFence` with a
    // zero timeout polls the value the backend has actually completed, which
    // is what "in flight" means here, and is allocation-free.
    [[nodiscard]] bool IsReached(const DeviceInterface& real, void* realInternal) const noexcept
    {
        return !Holds() || real.waitFence(realInternal, fence, value, 0).IsOk();
    }
};

// Applies one fence hold to a guard set, keeping the strongest entry for the
// fence it names. A full set replaces its least-protective entry when the new
// hold is stronger and otherwise stays as it is, which is quieter at destroy
// time, never stricter, and is a trace-level note rather than an error.
void StampGuard(Guard* guards, u32& guardCount, FenceHandle fence, u64 value) noexcept
{
    if (!fence.IsValid() || value == 0)
    {
        return;
    }
    for (u32 index = 0; index < guardCount; ++index)
    {
        if (guards[index].fence == fence)
        {
            if (value > guards[index].value)
            {
                guards[index].value = value;
            }
            return;
        }
    }
    if (guardCount < kMaxGuardsPerResource)
    {
        guards[guardCount].fence = fence;
        guards[guardCount].value = value;
        ++guardCount;
        return;
    }
    u32 weakest = 0;
    u64 weakestValue = guards[0].value;
    for (u32 index = 1; index < kMaxGuardsPerResource; ++index)
    {
        if (guards[index].value < weakestValue)
        {
            weakest = index;
            weakestValue = guards[index].value;
        }
    }
    if (weakestValue < value)
    {
        guards[weakest].fence = fence;
        guards[weakest].value = value;
        return;
    }
    CG_LOG(Trace, Rhi, "validating device: guard table full, a fence hold is untracked");
}

// ---------------------------------------------------------------------------
// Registry table keyed by real handles.
//
// `SlotMap` keys by handles it hands out itself; the wrapper needs the
// opposite — the backend hands out the handle and the wrapper records against
// it. Because the wrapper sees every create and destroy of the kinds it
// tracks (with swapchain images as the one intentional exception, which is
// documented at the header), a slot's index and its recorded generation track
// the backend's handle space exactly: `Register` writes the row the handle
// names, `Remove` empties it, and `Get`/`Contains` compare the stored
// generation against the one the handle carries. Unregistered indices between
// registered ones stay empty rows, which is the honest shape of a keyed table.
// ---------------------------------------------------------------------------

template <typename T, typename Tag>
class HandleTable
{
public:
    using Handle = cyber::memory::Handle<Tag>;

    explicit HandleTable(memory::Allocator& backing) noexcept : m_entries(backing) {}
    HandleTable(HandleTable&&) noexcept = default;
    HandleTable& operator=(HandleTable&&) noexcept = default;

    HandleTable(const HandleTable&) = delete;
    HandleTable& operator=(const HandleTable&) = delete;

    // Records `value` under a handle the backend handed out. False only when
    // the handle is invalid or the table cannot grow, which is the caller's
    // signal to release what it created and report `ErrorCode::Internal` so
    // nothing is tracked half-way.
    [[nodiscard]] bool Register(Handle handle, const T& value) noexcept
    {
        const Entry* entry = Ensure(handle);
        if (entry == nullptr)
        {
            return false;
        }
        m_entries[handle.index].generation = handle.generation;
        m_entries[handle.index].occupied = true;
        m_entries[handle.index].value = value;
        return true;
    }

    // Wrapper state of a live handle, or null when the handle is invalid, is
    // out of the table's range, or names a retired object.
    [[nodiscard]] T* Get(Handle handle) noexcept
    {
        Entry* entry = Find(handle);
        return entry != nullptr ? &entry->value : nullptr;
    }

    [[nodiscard]] const T* Get(Handle handle) const noexcept
    {
        const Entry* entry = Find(handle);
        return entry != nullptr ? &entry->value : nullptr;
    }

    // True when the handle names a live, registered object.
    [[nodiscard]] bool Contains(Handle handle) const noexcept
    {
        return Find(handle) != nullptr;
    }

    // Empties the handle's row. False when the handle does not name one, which
    // the destroy path reads as "forward untouched".
    [[nodiscard]] bool Remove(Handle handle) noexcept
    {
        Entry* entry = Find(handle);
        if (entry == nullptr)
        {
            return false;
        }
        entry->occupied = false;
        entry->generation = 1;
        return true;
    }

private:
    struct Entry
    {
        u32 generation = 1;
        bool occupied = false;
        T value{};
    };

    // The entry a handle names, or null. The table stores one row per backend
    // index it has ever seen; holes between indices are empty rows.
    [[nodiscard]] Entry* Find(Handle handle) noexcept
    {
        if (!handle.IsValid() || handle.index >= m_entries.Size())
        {
            return nullptr;
        }
        Entry& entry = m_entries[handle.index];
        return entry.occupied && entry.generation == handle.generation ? &entry : nullptr;
    }

    [[nodiscard]] const Entry* Find(Handle handle) const noexcept
    {
        return const_cast<HandleTable*>(this)->Find(handle);
    }

    [[nodiscard]] Entry* Ensure(Handle handle) noexcept
    {
        if (!handle.IsValid())
        {
            return nullptr;
        }
        while (m_entries.Size() <= handle.index)
        {
            Entry* pushed = m_entries.TryEmplaceBack();
            if (pushed == nullptr)
            {
                return nullptr;
            }
        }
        return &m_entries[handle.index];
    }

    DynamicArray<Entry> m_entries;
};

// Wrapper state for one live buffer: its label, for the error that names it,
// and the fence holds its destruction has to wait for.
struct TrackedBuffer
{
    const char* label = nullptr;
    Guard guards[kMaxGuardsPerResource] = {};
    u32 guardCount = 0;
};

// Wrapper state for one live texture: its description, which a view over it
// resolves its range against, its label, and the same fence holds.
struct TrackedTexture
{
    TextureDesc desc{};
    const char* label = nullptr;
    Guard guards[kMaxGuardsPerResource] = {};
    u32 guardCount = 0;
};

// Wrapper state for one live view: the texture it reads, so a pass that names
// the view stamps the texture the view resolves onto.
struct TrackedView
{
    TextureHandle texture{};
};

// Wrapper state for one live sampler.
struct TrackedSampler
{
};

// Wrapper state for one live shader: its stage, which the pipeline validators
// read through the existence probe.
struct TrackedShader
{
    ShaderStage stage = ShaderStage::Count;
};

// Wrapper state for one live bind group layout.
struct TrackedGroupLayout
{
};

// Wrapper state for one live bind group: the buffers and views it supplies, so
// a list that binds the group stamps what the group reads.
struct TrackedGroup
{
    Touch touches[kMaxTouchesPerSet] = {};
    u32 touchCount = 0;
};

// Wrapper state for one live pipeline layout.
struct TrackedPipelineLayout
{
};

// Wrapper state for one live pipeline or pipeline cache.
struct TrackedPipeline
{
};

// Wrapper state for one live command list. The record is allocated on its own
// so the pointer the command list interface receives stays valid while other
// lists are created — the same reason the null backend's record is. The table
// stores the pointer, and the device owns the record through its record list.
// The wrapper's own per-list record. The 'device' pointer resolves touch
// tracking inside callees; the mutex it guards is the device's own.
struct ValidatingDevice;

struct TrackedList
{
    ValidatingDevice* device = nullptr;
    const CommandListInterface* real = nullptr;
    CommandListHandle realHandle{};
    void* realInternal = nullptr;
    Touch touches[kMaxTouchesPerSet] = {};
    u32 touchCount = 0;

    // Records one touch, keeping the set distinct and bounded.
    void Record(const Touch& touch) noexcept
    {
        AddTouch(touches, touchCount, touch);
    }

    // Forgets what the list has named, which is what a fresh recording begins
    // over.
    void ClearTouches() noexcept
    {
        touchCount = 0;
    }
};

// The wrapper's per-device state. Created in `CreateValidatingDevice`, used by
// every entry of the wrapper's table below through its `internal` pointer, and
// released by the destroy entry when the front end releases the device.
struct ValidatingDevice
{
    explicit ValidatingDevice(memory::Allocator& backing) noexcept
        : buffers(backing), textures(backing), views(backing), samplers(backing), shaders(backing),
          groupLayouts(backing), groups(backing), pipelineLayouts(backing), pipelines(backing),
          caches(backing), lists(backing), listRecords(backing)
    {
    }

    memory::Allocator* allocator = nullptr;
    ValidationLevel level = ValidationLevel::Standard;
    const Backend* backend = nullptr;
    const DeviceInterface* real = nullptr;
    void* realInternal = nullptr;

    // Capabilities of the backend device, captured at creation so every
    // descriptor check compares against the truth of the device the caller
    // created.
    Capabilities capabilities{};

    // One lock over the wrapper's tables. Every table entry takes it for the
    // whole of its work, including the shared validators it runs, so the
    // existence probes below never lock on their own.
    mutable std::mutex mutex;

    HandleTable<TrackedBuffer, BufferTag> buffers;
    HandleTable<TrackedTexture, TextureTag> textures;
    HandleTable<TrackedView, TextureViewTag> views;
    HandleTable<TrackedSampler, SamplerTag> samplers;
    HandleTable<TrackedShader, ShaderTag> shaders;
    HandleTable<TrackedGroupLayout, BindGroupLayoutTag> groupLayouts;
    HandleTable<TrackedGroup, BindGroupTag> groups;
    HandleTable<TrackedPipelineLayout, PipelineLayoutTag> pipelineLayouts;
    HandleTable<TrackedPipeline, PipelineTag> pipelines;
    HandleTable<TrackedPipeline, PipelineCacheTag> caches;
    HandleTable<TrackedList*, CommandListTag> lists;
    // Per-list records this device owns, freed at teardown.
    DynamicArray<TrackedList*> listRecords;
};

// ---------------------------------------------------------------------------
// Existence probes for the shared validators.
//
// Every probe answers from the wrapper's own tables, which record exactly what
// the real backend handed back through the wrapper, so a handle is stale here
// for the same reason it is stale there: the destroy that retired it went
// through this table's own destroy entry. Probes never lock: they run inside
// a table entry that already holds the wrapper lock, and `std::mutex` is not
// recursive.
// ---------------------------------------------------------------------------

[[nodiscard]] bool ProbeBindGroupLayout(void* state, BindGroupLayoutHandle layout) noexcept
{
    auto* device = static_cast<const ValidatingDevice*>(state);
    CG_ASSERT(device != nullptr);
    return device->groupLayouts.Contains(layout);
}

[[nodiscard]] bool ProbePipelineLayout(void* state, PipelineLayoutHandle layout) noexcept
{
    auto* device = static_cast<const ValidatingDevice*>(state);
    CG_ASSERT(device != nullptr);
    return device->pipelineLayouts.Contains(layout);
}

[[nodiscard]] ShaderStage ProbeShaderStage(void* state, ShaderHandle shader) noexcept
{
    auto* device = static_cast<const ValidatingDevice*>(state);
    CG_ASSERT(device != nullptr);
    const TrackedShader* record = device->shaders.Get(shader);
    return record != nullptr ? record->stage : ShaderStage::Count;
}

[[nodiscard]] bool ProbeCache(void* state, PipelineCacheHandle cache) noexcept
{
    auto* device = static_cast<const ValidatingDevice*>(state);
    CG_ASSERT(device != nullptr);
    return device->caches.Contains(cache);
}

// Assembles the probe set the shared validators take. Called with the wrapper
// lock held; the probes assume it.
[[nodiscard]] validation::Existence ExistenceOf(const ValidatingDevice& device) noexcept
{
    validation::Existence existence{};
    existence.tables = const_cast<ValidatingDevice*>(&device);
    existence.pipelineLayout = &ProbePipelineLayout;
    existence.bindGroupLayout = &ProbeBindGroupLayout;
    existence.shaderStage = &ProbeShaderStage;
    existence.cache = &ProbeCache;
    return existence;
}

// -----------------------------------------------------------------------
// Destroy-path helpers, shared by buffers and textures.
// -----------------------------------------------------------------------

// First fence hold whose fence the backend still has not reached, or null when
// the destroy may proceed.
[[nodiscard]] const Guard* FindUnreachedGuard(const Guard* guards, u32 guardCount,
                                              const DeviceInterface& real,
                                              void* realInternal) noexcept
{
    for (u32 index = 0; index < guardCount; ++index)
    {
        if (!guards[index].IsReached(real, realInternal))
        {
            return &guards[index];
        }
    }
    return nullptr;
}

// The rejection a destroy of an in-flight resource reports. A labelled
// resource is named by its label; an unlabelled one by its packed handle,
// which a caller can match in a leak report or a log.
[[nodiscard]] Status<Error> RejectInFlightDestroy(const char* call, const char* kind,
                                                  const char* label, BufferHandle buffer,
                                                  TextureHandle texture,
                                                  const Guard& unreached) noexcept
{
    const u64 packedResource = texture.IsValid() ? PackHandle(texture) : PackHandle(buffer);
    CG_LOG(Warning, Rhi,
           "validating device rejected %s: %s '%s' (handle %#llx) is still in flight "
           "(fence %llu, reached at %llu)",
           call, kind, label != nullptr && label[0] != '\0' ? label : "unlabelled",
           static_cast<unsigned long long>(packedResource),
           static_cast<unsigned long long>(PackHandle(unreached.fence)),
           static_cast<unsigned long long>(unreached.value));
    // The specialized line above is this rejection's warning, so this logs once.
    const Error error{ErrorCode::InvalidArgument, "a resource still in flight was destroyed"};
    return Status<Error>::Err(error);
}

// Stamps a submission's signals onto one touched resource. Every signal of
// the submission becomes a hold, because the submission may read the resource
// until its work finishes and all of its signals are reached at once.
template <typename Capability>
void StampResource(Capability* record, const Span<const FenceSignal>& signals) noexcept
{
    for (usize index = 0; index < signals.Size(); ++index)
    {
        StampGuard(record->guards, record->guardCount, signals[index].fence, signals[index].value);
    }
}

// -----------------------------------------------------------------------
// Per-submission stamp set.
// -----------------------------------------------------------------------

// Distinct touches across the lists of one submission. Fixed size: submission
// is on the frame path and going over makes the check quieter, never stricter.
struct StampSet
{
    Touch entries[kMaxStampsPerSubmit] = {};
    u32 count = 0;

    void Record(const Touch& touch) noexcept
    {
        AddTouch(entries, count, touch);
    }
};

// Applies a submission's stamps: every touched resource that the wrapper
// tracks is stamped with the submission's signals. Called after the backend
// accepted the submission, which is what guarantees the work exists; a failed
// submit must not stamp, because no destroy could then be early.
void StampSubmission(ValidatingDevice& device, const StampSet& stamps,
                     const Span<const FenceSignal>& signals) noexcept
{
    for (u32 index = 0; index < stamps.count; ++index)
    {
        const Touch& touch = stamps.entries[index];
        if (!touch.NamesResource())
        {
            continue;
        }
        if (touch.isTexture)
        {
            TrackedTexture* record = device.textures.Get(touch.texture);
            if (record != nullptr)
            {
                StampResource(record, signals);
            }
        }
        else
        {
            TrackedBuffer* record = device.buffers.Get(touch.buffer);
            if (record != nullptr)
            {
                StampResource(record, signals);
            }
        }
    }
}

// Owner texture of a view, for the pass and bind wrappers that stamp
// attachments by texture. A view the table does not know yields an invalid
// handle, and an unresolvable touch is an untracked no-op.
[[nodiscard]] TextureHandle TextureOfView(const ValidatingDevice& device,
                                          TextureViewHandle view) noexcept
{
    const TrackedView* record = device.views.Get(view);
    return record != nullptr ? record->texture : TextureHandle{};
}

}  // namespace

// ---------------------------------------------------------------------------
// DeviceInterface entries. Every entry takes the wrapper state as its internal
// pointer, checks what the shared rules and the registry tables own, then
// forwards to the backend whose device state the wrapper holds.
// ---------------------------------------------------------------------------

// The wrapper's own per-list table, defined with the list entries below and
// named here so a command-list create can hand it out.
extern const CommandListInterface kValidatingListInterface;

Capabilities WrCapabilities(const void* internal) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr &&
              device->real->capabilities != nullptr);
    return device->real->capabilities(device->realInternal);
}

MemoryStats WrMemoryStats(const void* internal) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->memoryStats(device->realInternal);
}

QueueHandle WrQueue(const void* internal, QueueKind kind) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->queue(device->realInternal, kind);
}

Status<Error> WrCreateFence(void* internal, const FenceDesc& desc, FenceHandle* out) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->createFence(device->realInternal, desc, out);
}

Status<Error> WrDestroyFence(void* internal, FenceHandle fence) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->destroyFence(device->realInternal, fence);
}

Status<Error> WrSignalFence(void* internal, FenceHandle fence, u64 value) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->signalFence(device->realInternal, fence, value);
}

Status<Error> WrWaitFence(void* internal, FenceHandle fence, u64 value,
                          u64 timeoutNanoseconds) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->waitFence(device->realInternal, fence, value, timeoutNanoseconds);
}

u64 WrFenceValue(const void* internal, FenceHandle fence) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->fenceValue(device->realInternal, fence);
}

Status<Error> WrWaitIdle(void* internal) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->waitIdle(device->realInternal);
}

BufferInfo WrBufferInfo(const void* internal, BufferHandle buffer) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->bufferInfo(device->realInternal, buffer);
}

Status<Error> WrWriteBuffer(void* internal, BufferHandle buffer, u64 offset,
                            Span<const memory::Byte> bytes) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->writeBuffer(device->realInternal, buffer, offset, bytes);
}

Status<Error> WrReadBuffer(void* internal, BufferHandle buffer, u64 offset,
                           Span<memory::Byte> bytes) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->readBuffer(device->realInternal, buffer, offset, bytes);
}

ResourceState WrBufferState(const void* internal, BufferHandle buffer) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->bufferState(device->realInternal, buffer);
}

// A buffer create has the shared buffer rules and two registry jobs: record
// the wrapper state, and reject anything the real backend already refused
// before it touches the driver.
Status<Error> WrCreateBuffer(void* internal, const BufferDesc& desc, BufferHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    Error error{};
    if (!validation::ValidateBufferDesc(device->capabilities.limits, desc, &error))
    {
        CG_LOG(Warning, Rhi, "validating device rejected createBuffer: %s", error.message);
        return Status<Error>::Err(error);
    }

    const Status<Error> created = device->real->createBuffer(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    TrackedBuffer tracked{};
    tracked.label = desc.label;
    if (!device->buffers.Register(*out, tracked))
    {
        (void)device->real->destroyBuffer(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the buffer"});
    }
    return created;
}

// A destroy of a tracked buffer first checks the guards a submission put on
// it; an unreached hold is an error before the backend frees storage, because
// a real backend driver has no such check.
Status<Error> WrDestroyBuffer(void* internal, BufferHandle buffer) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    TrackedBuffer* record = device->buffers.Get(buffer);
    if (record != nullptr)
    {
        const Guard* unreached = FindUnreachedGuard(record->guards, record->guardCount,
                                                    *device->real, device->realInternal);
        if (unreached != nullptr)
        {
            return RejectInFlightDestroy("destroyBuffer", "buffer", record->label, buffer,
                                         TextureHandle{}, *unreached);
        }
    }

    const Status<Error> destroyed = device->real->destroyBuffer(device->realInternal, buffer);
    if (destroyed)
    {
        (void)device->buffers.Remove(buffer);
    }
    return destroyed;
}

Status<Error> WrCreateTexture(void* internal, const TextureDesc& desc, TextureHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    Error error{};
    if (!validation::ValidateTextureDesc(device->capabilities, desc, &error))
    {
        CG_LOG(Warning, Rhi, "validating device rejected createTexture: %s", error.message);
        return Status<Error>::Err(error);
    }

    const Status<Error> created = device->real->createTexture(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    TrackedTexture tracked{};
    tracked.desc = desc;
    tracked.label = desc.label;
    if (!device->textures.Register(*out, tracked))
    {
        (void)device->real->destroyTexture(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the texture"});
    }
    return created;
}

Status<Error> WrDestroyTexture(void* internal, TextureHandle texture) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    TrackedTexture* record = device->textures.Get(texture);
    if (record != nullptr)
    {
        const Guard* unreached = FindUnreachedGuard(record->guards, record->guardCount,
                                                    *device->real, device->realInternal);
        if (unreached != nullptr)
        {
            return RejectInFlightDestroy("destroyTexture", "texture", record->label, BufferHandle{},
                                         texture, *unreached);
        }
    }

    const Status<Error> destroyed = device->real->destroyTexture(device->realInternal, texture);
    if (destroyed)
    {
        (void)device->textures.Remove(texture);
    }
    return destroyed;
}

TextureInfo WrTextureInfo(const void* internal, TextureHandle texture) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->textureInfo(device->realInternal, texture);
}

// A view's shared rules need the range the texture resolves the view's range
// against; the wrapper keeps the create description of every live texture, so
// the view's texture descriptor is read from the wrapper's own table rather
// than from a second backend call.
Status<Error> WrCreateTextureView(void* internal, const TextureViewDesc& desc,
                                  TextureViewHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const TrackedTexture* textureRecord = device->textures.Get(desc.texture);
    if (textureRecord != nullptr)
    {
        TextureInfo info{};
        info.valid = true;
        info.dimension = textureRecord->desc.dimension;
        info.format = textureRecord->desc.format;
        info.usage = textureRecord->desc.usage;
        info.width = textureRecord->desc.width;
        info.height = textureRecord->desc.height;
        info.depth = textureRecord->desc.depth;
        info.arrayLayers = textureRecord->desc.arrayLayers;
        info.mipLevels = textureRecord->desc.mipLevels;
        info.sampleCount = textureRecord->desc.sampleCount;
        info.lifetime = textureRecord->desc.lifetime;

        Error error{};
        if (!validation::ValidateTextureView(info, desc, nullptr, &error))
        {
            CG_LOG(Warning, Rhi, "validating device rejected createTextureView: %s", error.message);
            return Status<Error>::Err(error);
        }
    }

    const Status<Error> created = device->real->createTextureView(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    TrackedView tracked{};
    tracked.texture = desc.texture;
    if (!device->views.Register(*out, tracked))
    {
        (void)device->real->destroyTextureView(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the texture view"});
    }
    return created;
}

// WrDestroyTextureView forwards without guard checks: views are not stamped
// by submissions, because every view's owner texture carries the holds.
Status<Error> WrDestroyTextureView(void* internal, TextureViewHandle view) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed = device->real->destroyTextureView(device->realInternal, view);
    if (destroyed)
    {
        (void)device->views.Remove(view);
    }
    return destroyed;
}

TextureViewInfo WrTextureViewInfo(const void* internal, TextureViewHandle view) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->textureViewInfo(device->realInternal, view);
}

ResourceState WrTextureState(const void* internal, TextureHandle texture, u32 mipLevel,
                             u32 arrayLayer) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->textureState(device->realInternal, texture, mipLevel, arrayLayer);
}

Status<Error> WrCreateSampler(void* internal, const SamplerDesc& desc, SamplerHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    Error error{};
    if (!validation::ValidateSamplerDesc(device->capabilities, desc, &error))
    {
        CG_LOG(Warning, Rhi, "validating device rejected createSampler: %s", error.message);
        return Status<Error>::Err(error);
    }

    const Status<Error> created = device->real->createSampler(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    const TrackedSampler tracked{};
    if (!device->samplers.Register(*out, tracked))
    {
        (void)device->real->destroySampler(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the sampler"});
    }
    return created;
}

Status<Error> WrDestroySampler(void* internal, SamplerHandle sampler) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed = device->real->destroySampler(device->realInternal, sampler);
    if (destroyed)
    {
        (void)device->samplers.Remove(sampler);
    }
    return destroyed;
}

// Transitions are the callee's own recorded state machine (the null backend's
// rules are shared already; this wrapper only forwards, because it does not
// track per-resource state itself).
Status<Error> WrTransition(void* internal, Span<const BufferBarrier> buffers,
                           Span<const TextureBarrier> textures) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->transition(device->realInternal, buffers, textures);
}

Status<Error> WrCreateShader(void* internal, const ShaderDesc& desc, ShaderHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    Error error{};
    if (!validation::ValidateShaderDesc(desc, &error))
    {
        CG_LOG(Warning, Rhi, "validating device rejected createShader: %s", error.message);
        return Status<Error>::Err(error);
    }

    const Status<Error> created = device->real->createShader(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    TrackedShader tracked{};
    tracked.stage = desc.stage;
    if (!device->shaders.Register(*out, tracked))
    {
        (void)device->real->destroyShader(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the shader"});
    }
    return created;
}

Status<Error> WrDestroyShader(void* internal, ShaderHandle shader) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed = device->real->destroyShader(device->realInternal, shader);
    if (destroyed)
    {
        (void)device->shaders.Remove(shader);
    }
    return destroyed;
}

ShaderInfo WrShaderInfo(const void* internal, ShaderHandle shader) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->shaderInfo(device->realInternal, shader);
}

Status<Error> WrCreateBindGroupLayout(void* internal, const BindGroupLayoutDesc& desc,
                                      BindGroupLayoutHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    Error error{};
    if (!validation::ValidateBindGroupLayout(device->capabilities.limits, desc, nullptr, &error))
    {
        CG_LOG(Warning, Rhi, "validating device rejected createBindGroupLayout: %s", error.message);
        return Status<Error>::Err(error);
    }

    const Status<Error> created =
        device->real->createBindGroupLayout(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    const TrackedGroupLayout tracked{};
    if (!device->groupLayouts.Register(*out, tracked))
    {
        (void)device->real->destroyBindGroupLayout(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the bind group layout"});
    }
    return created;
}

Status<Error> WrDestroyBindGroupLayout(void* internal, BindGroupLayoutHandle layout) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed =
        device->real->destroyBindGroupLayout(device->realInternal, layout);
    if (destroyed)
    {
        (void)device->groupLayouts.Remove(layout);
    }
    return destroyed;
}

// A bind group's touches are built from the resources it supplies: a buffer
// range stamps its buffer, and a view stamps the texture the view reads.
Status<Error> WrCreateBindGroup(void* internal, const BindGroupDesc& desc,
                                BindGroupHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> created = device->real->createBindGroup(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }

    TrackedGroup tracked{};
    tracked.touchCount = 0;
    for (usize index = 0; index < desc.resources.Size(); ++index)
    {
        const BindingResource& resource = desc.resources[index];
        if (resource.type == BindingType::UniformBuffer ||
            resource.type == BindingType::StorageBuffer ||
            resource.type == BindingType::ReadOnlyStorageBuffer)
        {
            AddTouch(tracked.touches, tracked.touchCount, TouchOfBuffer(resource.buffer));
            continue;
        }
        if (resource.type == BindingType::SampledTexture ||
            resource.type == BindingType::StorageTexture)
        {
            const TrackedView* viewRecord = device->views.Get(resource.textureView);
            if (viewRecord != nullptr)
            {
                AddTouch(tracked.touches, tracked.touchCount, TouchOfTexture(viewRecord->texture));
            }
        }
    }
    if (!device->groups.Register(*out, tracked))
    {
        (void)device->real->destroyBindGroup(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the bind group"});
    }
    return created;
}

Status<Error> WrUpdateBindGroup(void* internal, BindGroupHandle group,
                                Span<const BindingResource> resources) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    TrackedGroup* tracked = device->groups.Get(group);
    if (tracked == nullptr)
    {
        return Status<Error>::Err(
            Error{ErrorCode::InvalidArgument,
                  "validating device: bind group handle is stale or invalid"});
    }
    const Status<Error> updated =
        device->real->updateBindGroup(device->realInternal, group, resources);
    if (!updated)
    {
        return updated;
    }

    tracked->touchCount = 0;
    for (usize index = 0; index < resources.Size(); ++index)
    {
        const BindingResource& resource = resources[index];
        if (resource.type == BindingType::UniformBuffer ||
            resource.type == BindingType::StorageBuffer ||
            resource.type == BindingType::ReadOnlyStorageBuffer)
        {
            AddTouch(tracked->touches, tracked->touchCount, TouchOfBuffer(resource.buffer));
        }
        else if (resource.type == BindingType::SampledTexture ||
                 resource.type == BindingType::StorageTexture)
        {
            const TrackedView* viewRecord = device->views.Get(resource.textureView);
            if (viewRecord != nullptr)
            {
                AddTouch(tracked->touches, tracked->touchCount,
                         TouchOfTexture(viewRecord->texture));
            }
        }
    }
    return updated;
}

Status<Error> WrDestroyBindGroup(void* internal, BindGroupHandle group) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed = device->real->destroyBindGroup(device->realInternal, group);
    if (destroyed)
    {
        (void)device->groups.Remove(group);
    }
    return destroyed;
}

BindGroupInfo WrBindGroupInfo(const void* internal, BindGroupHandle group) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->bindGroupInfo(device->realInternal, group);
}

Status<Error> WrCreatePipelineLayout(void* internal, const PipelineLayoutDesc& desc,
                                     PipelineLayoutHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const validation::Existence live = ExistenceOf(*device);
    Error error{};
    if (!validation::ValidatePipelineLayout(device->capabilities, live, desc, &error))
    {
        CG_LOG(Warning, Rhi, "validating device rejected createPipelineLayout: %s", error.message);
        return Status<Error>::Err(error);
    }

    const Status<Error> created =
        device->real->createPipelineLayout(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    const TrackedPipelineLayout tracked{};
    if (!device->pipelineLayouts.Register(*out, tracked))
    {
        (void)device->real->destroyPipelineLayout(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the pipeline layout"});
    }
    return created;
}

Status<Error> WrDestroyPipelineLayout(void* internal, PipelineLayoutHandle layout) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed =
        device->real->destroyPipelineLayout(device->realInternal, layout);
    if (destroyed)
    {
        (void)device->pipelineLayouts.Remove(layout);
    }
    return destroyed;
}

Status<Error> WrCreateGraphicsPipeline(void* internal, const GraphicsPipelineDesc& desc,
                                       PipelineHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const validation::Existence live = ExistenceOf(*device);
    Error error{};
    if (!validation::ValidateGraphicsPipelineDesc(device->capabilities, live, desc, &error))
    {
        CG_LOG(Warning, Rhi, "validating device rejected createGraphicsPipeline: %s",
               error.message);
        return Status<Error>::Err(error);
    }

    const Status<Error> created =
        device->real->createGraphicsPipeline(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    const TrackedPipeline tracked{};
    if (!device->pipelines.Register(*out, tracked))
    {
        (void)device->real->destroyGraphicsPipeline(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the pipeline"});
    }
    return created;
}

Status<Error> WrDestroyGraphicsPipeline(void* internal, PipelineHandle pipeline) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed =
        device->real->destroyGraphicsPipeline(device->realInternal, pipeline);
    if (destroyed)
    {
        (void)device->pipelines.Remove(pipeline);
    }
    return destroyed;
}

Status<Error> WrCreateComputePipeline(void* internal, const ComputePipelineDesc& desc,
                                      PipelineHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const validation::Existence live = ExistenceOf(*device);
    Error error{};
    if (!validation::ValidateComputePipelineDesc(device->capabilities, live, desc, &error))
    {
        CG_LOG(Warning, Rhi, "validating device rejected createComputePipeline: %s", error.message);
        return Status<Error>::Err(error);
    }

    const Status<Error> created =
        device->real->createComputePipeline(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    const TrackedPipeline tracked{};
    if (!device->pipelines.Register(*out, tracked))
    {
        (void)device->real->destroyComputePipeline(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the pipeline"});
    }
    return created;
}

Status<Error> WrDestroyComputePipeline(void* internal, PipelineHandle pipeline) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed =
        device->real->destroyComputePipeline(device->realInternal, pipeline);
    if (destroyed)
    {
        (void)device->pipelines.Remove(pipeline);
    }
    return destroyed;
}

Status<Error> WrCreatePipelineCache(void* internal, const PipelineCacheDesc& desc,
                                    PipelineCacheHandle* out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> created =
        device->real->createPipelineCache(device->realInternal, desc, out);
    if (!created)
    {
        return created;
    }
    const TrackedPipeline tracked{};
    if (!device->caches.Register(*out, tracked))
    {
        (void)device->real->destroyPipelineCache(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the pipeline cache"});
    }
    return created;
}

Status<Error> WrDestroyPipelineCache(void* internal, PipelineCacheHandle cache) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> destroyed = device->real->destroyPipelineCache(device->realInternal, cache);
    if (destroyed)
    {
        (void)device->caches.Remove(cache);
    }
    return destroyed;
}

Status<Error> WrPipelineCacheBytes(const void* internal, PipelineCacheHandle cache,
                                   memory::Allocator& allocator,
                                   DynamicArray<memory::Byte>* out) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->pipelineCacheBytes(device->realInternal, cache, allocator, out);
}

PipelineInfo WrPipelineInfo(const void* internal, PipelineHandle pipeline) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->pipelineInfo(device->realInternal, pipeline);
}

// A command list is wrapped after the backend creates its own: the wrapper
// record is what the caller's list carries, and the real list is what the
// record remembers.
Status<Error> WrCreateCommandList(void* internal, const CommandListDesc& desc,
                                  CommandListHandle* out, CommandListCreation* creation) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> created =
        device->real->createCommandList(device->realInternal, desc, out, creation);
    if (!created)
    {
        return created;
    }

    void* memory = device->allocator->Allocate(sizeof(TrackedList), alignof(TrackedList));
    if (memory == nullptr)
    {
        (void)device->real->destroyCommandList(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the command list"});
    }
    TrackedList* record = new (memory) TrackedList{};
    record->device = device;
    record->real = creation->interface;
    record->realInternal = creation->internal;
    record->realHandle = *out;

    TrackedList** slot = device->listRecords.TryEmplaceBack();
    if (slot != nullptr)
    {
        *slot = record;
    }
    const bool registered = device->lists.Register(*out, record);
    if (slot == nullptr || !registered)
    {
        record->~TrackedList();
        device->allocator->Deallocate(record);
        (void)device->real->destroyCommandList(device->realInternal, *out);
        return Status<Error>::Err(
            Error{ErrorCode::Internal, "validating device could not track the command list"});
    }

    creation->interface = &kValidatingListInterface;
    creation->internal = record;
    return created;
}

Status<Error> WrDestroyCommandList(void* internal, CommandListHandle list) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    TrackedList** slotPtr = device->lists.Get(list);
    TrackedList* record = slotPtr != nullptr ? *slotPtr : nullptr;
    const Status<Error> destroyed = device->real->destroyCommandList(
        device->realInternal, record != nullptr ? record->realHandle : list);
    if (!destroyed)
    {
        return destroyed;
    }
    if (record != nullptr)
    {
        (void)device->lists.Remove(list);
        for (usize index = 0; index < device->listRecords.Size(); ++index)
        {
            if (device->listRecords[index] == record)
            {
                device->listRecords[index] = nullptr;
                break;
            }
        }
        record->real = nullptr;
        record->realInternal = nullptr;
        record->~TrackedList();
        device->allocator->Deallocate(record);
    }
    return destroyed;
}

// A submission stamps the touches of its lists so the work named by those
// resources is protected from an early destroy. The set is built from the
// wrapper's list records, not from the backend's: on submit the wrapper never
// reads the backend's named caches beyond the fence values.
Status<Error> WrSubmit(void* internal, const SubmitDesc& desc) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    std::lock_guard<std::mutex> lock(device->mutex);

    StampSet stamps{};
    for (usize index = 0; index < desc.commandLists.Size(); ++index)
    {
        TrackedList** slotPtr = device->lists.Get(desc.commandLists[index]);
        const TrackedList* record = slotPtr != nullptr ? *slotPtr : nullptr;
        if (record == nullptr)
        {
            continue;
        }
        for (u32 touchIndex = 0; touchIndex < record->touchCount; ++touchIndex)
        {
            stamps.Record(record->touches[touchIndex]);
        }
    }

    const Status<Error> submitted = device->real->submit(device->realInternal, desc);
    if (submitted)
    {
        StampSubmission(*device, stamps, desc.signals);
    }
    return submitted;
}

SubmissionInfo WrSubmissionInfo(const void* internal, QueueKind kind) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->submissionInfo(device->realInternal, kind);
}

Status<Error> WrCreateSwapChain(void* internal, const SwapChainDesc& desc, SwapChainHandle* out,
                                SwapChainCreation* creation) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->createSwapChain(device->realInternal, desc, out, creation);
}

Status<Error> WrDestroySwapChain(void* internal, SwapChainHandle swapchain) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->destroySwapChain(device->realInternal, swapchain);
}

ObjectNameInfo WrObjectName(const void* internal, const ObjectRef& object) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->objectName(device->realInternal, object);
}

Status<Error> WrCreateTimestampQueryHeap(void* internal, const TimestampQueryHeapDesc& desc,
                                         TimestampQueryHeapHandle* out) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->createTimestampQueryHeap(device->realInternal, desc, out);
}

Status<Error> WrDestroyTimestampQueryHeap(void* internal, TimestampQueryHeapHandle heap) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->destroyTimestampQueryHeap(device->realInternal, heap);
}

Status<Error> WrReadTimestamps(void* internal, TimestampQueryHeapHandle heap, u32 firstQuery,
                               u32 queryCount, FenceHandle completionFence, u64 completionValue,
                               memory::Span<u64> out) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->readTimestamps(device->realInternal, heap, firstQuery, queryCount,
                                        completionFence, completionValue, out);
}

u64 WrTimestampFrequency(const void* internal, QueueKind queue) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->timestampFrequency(device->realInternal, queue);
}

u32 WrReportLeaks(const void* internal) noexcept
{
    const auto* device = static_cast<const ValidatingDevice*>(internal);
    CG_ASSERT(device != nullptr && device->real != nullptr);
    return device->real->reportLeaks(device->realInternal);
}

// The wrapper's own per-device table, forwarding to the backend the wrapper
// wraps. Static lifetime so `Device::Release` can keep using it while other
// devices exist.
const DeviceInterface kValidatingDeviceInterface = {
    &WrCapabilities,
    &WrMemoryStats,
    &WrQueue,
    &WrCreateFence,
    &WrDestroyFence,
    &WrSignalFence,
    &WrWaitFence,
    &WrFenceValue,
    &WrCreateTimestampQueryHeap,
    &WrDestroyTimestampQueryHeap,
    &WrReadTimestamps,
    &WrTimestampFrequency,
    &WrReportLeaks,
    &WrWaitIdle,
    &WrCreateBuffer,
    &WrDestroyBuffer,
    &WrBufferInfo,
    &WrWriteBuffer,
    &WrReadBuffer,
    &WrBufferState,
    &WrCreateTexture,
    &WrDestroyTexture,
    &WrTextureInfo,
    &WrCreateTextureView,
    &WrDestroyTextureView,
    &WrTextureViewInfo,
    &WrTextureState,
    &WrCreateSampler,
    &WrDestroySampler,
    &WrTransition,
    &WrCreateShader,
    &WrDestroyShader,
    &WrShaderInfo,
    &WrCreateBindGroupLayout,
    &WrDestroyBindGroupLayout,
    &WrCreateBindGroup,
    &WrUpdateBindGroup,
    &WrDestroyBindGroup,
    &WrBindGroupInfo,
    &WrCreatePipelineLayout,
    &WrDestroyPipelineLayout,
    &WrCreateGraphicsPipeline,
    &WrDestroyGraphicsPipeline,
    &WrCreateComputePipeline,
    &WrDestroyComputePipeline,
    &WrCreatePipelineCache,
    &WrDestroyPipelineCache,
    &WrPipelineCacheBytes,
    &WrPipelineInfo,
    &WrCreateCommandList,
    &WrDestroyCommandList,
    &WrSubmit,
    &WrSubmissionInfo,
    &WrCreateSwapChain,
    &WrDestroySwapChain,
    &WrObjectName,
};

// ---------------------------------------------------------------------------
// CommandListInterface entries. Every entry is a list call: a check the list's
// own requested behaviour runs first, then the real list performs its work,
// and the wrapper's tracking follows behind only on success.
// ---------------------------------------------------------------------------

Status<Error> WrListBegin(void* internal) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> begun = list->real->begin(list->realInternal);
    if (begun)
    {
        list->ClearTouches();
    }
    return begun;
}

Status<Error> WrListEnd(void* internal) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->end(list->realInternal);
}

void WrListReset(void* internal) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    list->real->reset(list->realInternal);
    list->ClearTouches();
}

Status<Error> WrListBeginRenderPass(void* internal, const RenderPassDesc& desc) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr && list->device != nullptr);
    ValidatingDevice* device = list->device;
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> begun = list->real->beginRenderPass(list->realInternal, desc);
    if (!begun)
    {
        return begun;
    }
    for (usize index = 0; index < desc.colorAttachments.Size(); ++index)
    {
        const ColorAttachment& attachment = desc.colorAttachments[index];
        list->Record(TouchOfTexture(TextureOfView(*device, attachment.view)));
        if (attachment.resolveView.IsValid())
        {
            list->Record(TouchOfTexture(TextureOfView(*device, attachment.resolveView)));
        }
    }
    if (desc.hasDepthStencil)
    {
        list->Record(TouchOfTexture(TextureOfView(*device, desc.depthStencil.view)));
    }
    return begun;
}

Status<Error> WrListEndRenderPass(void* internal) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->endRenderPass(list->realInternal);
}

Status<Error> WrListSetPipeline(void* internal, PipelineHandle pipeline) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->setPipeline(list->realInternal, pipeline);
}

Status<Error> WrListSetBindGroup(void* internal, BindGroupSlot slot, BindGroupHandle group,
                                 Span<const u64> dynamicOffsets) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr && list->device != nullptr);
    ValidatingDevice* device = list->device;
    std::lock_guard<std::mutex> lock(device->mutex);

    const Status<Error> set =
        list->real->setBindGroup(list->realInternal, slot, group, dynamicOffsets);
    if (!set)
    {
        return set;
    }
    TrackedGroup* groupRecord = device->groups.Get(group);
    if (groupRecord != nullptr)
    {
        for (u32 index = 0; index < groupRecord->touchCount; ++index)
        {
            list->Record(groupRecord->touches[index]);
        }
    }
    return set;
}

Status<Error> WrListSetVertexBuffer(void* internal, u32 binding, BufferHandle buffer,
                                    u64 offsetBytes) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> set =
        list->real->setVertexBuffer(list->realInternal, binding, buffer, offsetBytes);
    if (set)
    {
        list->Record(TouchOfBuffer(buffer));
    }
    return set;
}

Status<Error> WrListSetIndexBuffer(void* internal, BufferHandle buffer, u64 offsetBytes,
                                   IndexFormat format) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> set =
        list->real->setIndexBuffer(list->realInternal, buffer, offsetBytes, format);
    if (set)
    {
        list->Record(TouchOfBuffer(buffer));
    }
    return set;
}

Status<Error> WrListSetViewport(void* internal, const Viewport& viewport) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->setViewport(list->realInternal, viewport);
}

Status<Error> WrListSetScissor(void* internal, const ScissorRect& scissor) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->setScissor(list->realInternal, scissor);
}

Status<Error> WrListDraw(void* internal, const DrawArguments& arguments) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->draw(list->realInternal, arguments);
}

Status<Error> WrListDrawIndexed(void* internal, const DrawIndexedArguments& arguments) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->drawIndexed(list->realInternal, arguments);
}

Status<Error> WrListDrawIndirect(void* internal, BufferHandle arguments, u64 offsetBytes,
                                 u32 drawCount, u32 strideBytes) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> drawn = list->real->drawIndirect(list->realInternal, arguments, offsetBytes,
                                                         drawCount, strideBytes);
    if (drawn)
    {
        list->Record(TouchOfBuffer(arguments));
    }
    return drawn;
}

Status<Error> WrListDrawIndexedIndirect(void* internal, BufferHandle arguments, u64 offsetBytes,
                                        u32 drawCount, u32 strideBytes) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> drawn = list->real->drawIndexedIndirect(
        list->realInternal, arguments, offsetBytes, drawCount, strideBytes);
    if (drawn)
    {
        list->Record(TouchOfBuffer(arguments));
    }
    return drawn;
}

Status<Error> WrListDispatch(void* internal, const DispatchArguments& arguments) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->dispatch(list->realInternal, arguments);
}

Status<Error> WrListDispatchIndirect(void* internal, BufferHandle arguments,
                                     u64 offsetBytes) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> dispatched =
        list->real->dispatchIndirect(list->realInternal, arguments, offsetBytes);
    if (dispatched)
    {
        list->Record(TouchOfBuffer(arguments));
    }
    return dispatched;
}

Status<Error> WrListCopyBuffer(void* internal, const BufferCopy& copy) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> copied = list->real->copyBuffer(list->realInternal, copy);
    if (copied)
    {
        list->Record(TouchOfBuffer(copy.source));
        list->Record(TouchOfBuffer(copy.destination));
    }
    return copied;
}

Status<Error> WrListCopyBufferToTexture(void* internal, const BufferTextureCopy& copy) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> copied = list->real->copyBufferToTexture(list->realInternal, copy);
    if (copied)
    {
        list->Record(TouchOfBuffer(copy.buffer));
        list->Record(TouchOfTexture(copy.texture));
    }
    return copied;
}

Status<Error> WrListCopyTextureToBuffer(void* internal, const BufferTextureCopy& copy) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> copied = list->real->copyTextureToBuffer(list->realInternal, copy);
    if (copied)
    {
        list->Record(TouchOfTexture(copy.texture));
        list->Record(TouchOfBuffer(copy.buffer));
    }
    return copied;
}

Status<Error> WrListCopyTexture(void* internal, const TextureCopy& copy) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> copied = list->real->copyTexture(list->realInternal, copy);
    if (copied)
    {
        list->Record(TouchOfTexture(copy.source));
        list->Record(TouchOfTexture(copy.destination));
    }
    return copied;
}

Status<Error> WrListClearColorTexture(void* internal, TextureHandle texture,
                                      const ClearColorValue& value,
                                      const SubresourceRange& range) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> cleared =
        list->real->clearColorTexture(list->realInternal, texture, value, range);
    if (cleared)
    {
        list->Record(TouchOfTexture(texture));
    }
    return cleared;
}

Status<Error> WrListClearDepthStencilTexture(void* internal, TextureHandle texture,
                                             const ClearDepthStencilValue& value,
                                             const SubresourceRange& range) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> cleared =
        list->real->clearDepthStencilTexture(list->realInternal, texture, value, range);
    if (cleared)
    {
        list->Record(TouchOfTexture(texture));
    }
    return cleared;
}

Status<Error> WrListBarrier(void* internal, Span<const BufferBarrier> buffers,
                            Span<const TextureBarrier> textures) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    const Status<Error> applied = list->real->barrier(list->realInternal, buffers, textures);
    if (applied)
    {
        for (usize index = 0; index < buffers.Size(); ++index)
        {
            list->Record(TouchOfBuffer(buffers[index].buffer));
        }
        for (usize index = 0; index < textures.Size(); ++index)
        {
            list->Record(TouchOfTexture(textures[index].texture));
        }
    }
    return applied;
}

Status<Error> WrListPushConstants(void* internal, ShaderStageMask stages, u32 offsetBytes,
                                  Span<const memory::Byte> data) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->pushConstants(list->realInternal, stages, offsetBytes, data);
}

Status<Error> WrListBeginDebugLabel(void* internal, const char* label) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->beginDebugLabel(list->realInternal, label);
}

Status<Error> WrListEndDebugLabel(void* internal) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->endDebugLabel(list->realInternal);
}

Status<Error> WrListSetDebugMarker(void* internal, const char* label) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->setDebugMarker(list->realInternal, label);
}

Status<Error> WrListWriteTimestamp(void* internal, TimestampQueryHeapHandle heap,
                                   u32 queryIndex) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->writeTimestamp(list->realInternal, heap, queryIndex);
}

Status<Error> WrListResolveTimestamps(void* internal, TimestampQueryHeapHandle heap, u32 firstQuery,
                                      u32 queryCount) noexcept
{
    auto* list = static_cast<TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->resolveTimestamps(list->realInternal, heap, firstQuery, queryCount);
}

RenderPassInfo WrListCurrentRenderPass(const void* internal) noexcept
{
    const auto* list = static_cast<const TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->currentRenderPass(list->realInternal);
}

CommandListStats WrListStats(const void* internal) noexcept
{
    const auto* list = static_cast<const TrackedList*>(internal);
    CG_ASSERT(list != nullptr && list->real != nullptr);
    return list->real->stats(list->realInternal);
}

// The wrapper's own per-list table. Static lifetime for the same reason the
// device table is.
const CommandListInterface kValidatingListInterface = {
    &WrListBegin,
    &WrListEnd,
    &WrListReset,
    &WrListBeginRenderPass,
    &WrListEndRenderPass,
    &WrListSetPipeline,
    &WrListSetBindGroup,
    &WrListSetVertexBuffer,
    &WrListSetIndexBuffer,
    &WrListSetViewport,
    &WrListSetScissor,
    &WrListDraw,
    &WrListDrawIndexed,
    &WrListDrawIndirect,
    &WrListDrawIndexedIndirect,
    &WrListDispatch,
    &WrListDispatchIndirect,
    &WrListCopyBuffer,
    &WrListCopyBufferToTexture,
    &WrListCopyTextureToBuffer,
    &WrListCopyTexture,
    &WrListClearColorTexture,
    &WrListClearDepthStencilTexture,
    &WrListBarrier,
    &WrListPushConstants,
    &WrListBeginDebugLabel,
    &WrListEndDebugLabel,
    &WrListSetDebugMarker,
    &WrListWriteTimestamp,
    &WrListResolveTimestamps,
    &WrListCurrentRenderPass,
    &WrListStats,
};

// ---------------------------------------------------------------------------
// Creation and destruction.
// ---------------------------------------------------------------------------

Result<DeviceCreation, Error> CreateValidatingDevice(const DeviceCreation& real,
                                                     const Backend& backend, ValidationLevel level,
                                                     memory::Allocator& allocator) noexcept
{
    if (real.interface == nullptr || real.internal == nullptr)
    {
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::Internal, "validating device wrapped an incomplete backend device"});
    }

    void* memory = allocator.Allocate(sizeof(ValidatingDevice), alignof(ValidatingDevice));
    if (memory == nullptr)
    {
        return Result<DeviceCreation, Error>::Err(
            Error{ErrorCode::Internal, "validating device could not allocate its state"});
    }
    ValidatingDevice* device = new (memory) ValidatingDevice(allocator);
    device->allocator = &allocator;
    device->level = level;
    device->backend = &backend;
    device->real = real.interface;
    device->realInternal = real.internal;
    device->capabilities = real.interface->capabilities(real.internal);

    DeviceCreation creation{};
    creation.interface = &kValidatingDeviceInterface;
    creation.internal = device;
    return Result<DeviceCreation, Error>::Ok(creation);
}

void DestroyValidatingDevice(void* internal) noexcept
{
    auto* device = static_cast<ValidatingDevice*>(internal);
    if (device == nullptr)
    {
        return;
    }

    const DeviceInterface* real = device->real;
    const Backend* backend = device->backend;
    void* realInternal = device->realInternal;
    memory::Allocator* backing = device->allocator;
    CG_ASSERT(backing != nullptr);

    // The real device is destroyed first, while the records still hold its
    // tables; the backend's destroy path resets what its own device owns.
    if (real != nullptr && real->waitIdle != nullptr)
    {
        (void)real->waitIdle(realInternal);
    }
    if (device->level == ValidationLevel::Exhaustive && real != nullptr &&
        real->reportLeaks != nullptr)
    {
        (void)real->reportLeaks(realInternal);
    }
    if (backend != nullptr && backend->destroyDevice != nullptr)
    {
        backend->destroyDevice(realInternal);
    }

    for (usize index = 0; index < device->listRecords.Size(); ++index)
    {
        TrackedList* record = device->listRecords[index];
        if (record != nullptr)
        {
            record->real = nullptr;
            record->realInternal = nullptr;
            record->~TrackedList();
            backing->Deallocate(record);
        }
    }
    device->~ValidatingDevice();
    backing->Deallocate(device);
}

bool IsWrapperInterface(const DeviceInterface& interface) noexcept
{
    return &interface == &kValidatingDeviceInterface;
}

}  // namespace cyber::rhi
