#pragma once

#include <cyber/core/types.h>
#include <cyber/memory/span.h>
#include <cyber/rhi/handle.h>
#include <cyber/rhi/shader.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

// The four binding slots, fixed by how often they change. The slot order is the
// pipeline layout order and the bind order; it is not a backend limit.
enum class BindGroupSlot : u8
{
    // Once per frame: camera, time, the frame's light buffer.
    PerFrame = 0,
    // Once per view: a shadow atlas, a reflection probe set.
    PerView,
    // Once per material: material constants, textures, and samplers.
    PerMaterial,
    // Once per draw: object or instance data.
    PerDraw,
    // Count of enumerators, for table tests. Not a slot.
    Count
};

// Number of real slots, not counting `Count`.
inline constexpr u32 kBindGroupSlotCount = static_cast<u32>(BindGroupSlot::Count);

// Human-readable slot, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* BindGroupSlotName(BindGroupSlot slot) noexcept
{
    switch (slot)
    {
    case BindGroupSlot::PerFrame:
        return "per_frame";
    case BindGroupSlot::PerView:
        return "per_view";
    case BindGroupSlot::PerMaterial:
        return "per_material";
    case BindGroupSlot::PerDraw:
        return "per_draw";
    case BindGroupSlot::Count:
        break;
    }
    return "unknown";
}

// What one binding slot holds.
enum class BindingType : u8
{
    // A constant buffer range, read-only in a shader.
    UniformBuffer = 0,
    // A structured or raw buffer a shader reads and writes.
    StorageBuffer,
    // A texture view sampled through a separate sampler binding.
    SampledTexture,
    // A texture view written from a shader.
    StorageTexture,
    // A filter and address state object.
    Sampler,
    // Count of enumerators, for table tests. Not a type.
    Count
};

// Number of real binding types, not counting `Count`.
inline constexpr u32 kBindingTypeCount = static_cast<u32>(BindingType::Count);

// Human-readable binding type, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* BindingTypeName(BindingType type) noexcept
{
    switch (type)
    {
    case BindingType::UniformBuffer:
        return "uniform_buffer";
    case BindingType::StorageBuffer:
        return "storage_buffer";
    case BindingType::SampledTexture:
        return "sampled_texture";
    case BindingType::StorageTexture:
        return "storage_texture";
    case BindingType::Sampler:
        return "sampler";
    case BindingType::Count:
        break;
    }
    return "unknown";
}

// Largest number of push-constant ranges one pipeline layout may declare.
inline constexpr u32 kMaxPushConstantRanges = 4;

// One binding slot in a group layout.
struct BindGroupLayoutEntry
{
    // Slot number inside the group, below `Limits::maxBindingsPerGroup` and
    // unique within the layout.
    u32 binding = 0;
    // What the slot holds.
    BindingType type = BindingType::UniformBuffer;
    // Stages that may read the slot. Empty visibility is invalid.
    ShaderStageMask visibility = 0;
    // Number of resources the binding consumes. A value above 1 is an array
    // indexed at run time; the bind group supplies that many resources in a row.
    u32 arrayCount = 1;
};

// Description of a bind group layout.
struct BindGroupLayoutDesc
{
    // Entries in binding order. Between one and `Limits::maxBindingsPerGroup`.
    memory::Span<const BindGroupLayoutEntry> entries{};
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// One resource supplied to a bind group. Every field is meaningful only for the
// matching `type`; the rest must stay zeroed.
struct BindingResource
{
    // Must match the type the layout declares for this position.
    BindingType type = BindingType::UniformBuffer;
    // Buffer for `UniformBuffer` or `StorageBuffer`.
    BufferHandle buffer;
    // Byte offset of the range inside the buffer.
    u64 offset = 0;
    // Size of the range in bytes. Zero means "from `offset` to the end".
    u64 size = 0;
    // View for `SampledTexture` or `StorageTexture`.
    TextureViewHandle textureView;
    // Sampler for `Sampler`.
    SamplerHandle sampler;
};

// Description of a bind group. The resources are flattened in binding order: a
// layout whose array counts are 1, 3, 1 takes five resources.
struct BindGroupDesc
{
    // Layout the group conforms to. Must be a live handle.
    BindGroupLayoutHandle layout;
    // Resources in binding order. The count must equal the sum of the layout's
    // array counts.
    memory::Span<const BindingResource> resources{};
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// What a created bind group reports about itself. `valid` is false for a stale
// handle, in which case every other field is zero.
struct BindGroupInfo
{
    bool valid = false;
    BindGroupLayoutHandle layout;
    u32 resourceCount = 0;
};

// A byte range of push constants visible to some stages.
//
// D3D12 calls these root constants and Vulkan push constants; both are a small
// block written per draw with no descriptor. Ranges may not overlap for stages
// they share.
struct PushConstantRange
{
    // Byte offset inside the push-constant block. A multiple of 4.
    u32 offsetBytes = 0;
    // Size in bytes. A multiple of 4, non-zero, and within
    // `Limits::maxPushConstantBytes` once offset is added.
    u32 sizeBytes = 0;
    // Stages that can read the range.
    ShaderStageMask stages = 0;
};

// Description of a pipeline layout: one group layout per slot, plus push
// constants.
struct PipelineLayoutDesc
{
    // Group layout per slot, indexed by `BindGroupSlot`. An invalid handle means
    // the slot is unused. At least one slot must be used.
    BindGroupLayoutHandle groups[kBindGroupSlotCount];
    // Push-constant ranges. Up to `kMaxPushConstantRanges`, or none.
    memory::Span<const PushConstantRange> pushConstants{};
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

}  // namespace cyber::rhi
