#pragma once

#include <cyber/core/types.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

// Graphics API a backend drives.
enum class BackendKind : u8
{
    // The validation backend. It allocates nothing on the GPU and checks every
    // call.
    Null = 0,
    // Direct3D 12 on Windows (Phase 14).
    D3D12,
    // Vulkan on Windows and Linux (Phase 34).
    Vulkan,
    // Metal on Apple platforms (later).
    Metal,
    // WebGPU (later).
    WebGpu,
    // Count of enumerators, for table tests. Not a backend.
    Count
};

// Enumerators `BackendKind` has, `Count` included.
inline constexpr u32 kBackendKindCount = static_cast<u32>(BackendKind::Count);

// Human-readable backend name, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* BackendKindName(BackendKind kind) noexcept
{
    switch (kind)
    {
    case BackendKind::Null:
        return "null";
    case BackendKind::D3D12:
        return "d3d12";
    case BackendKind::Vulkan:
        return "vulkan";
    case BackendKind::Metal:
        return "metal";
    case BackendKind::WebGpu:
        return "webgpu";
    case BackendKind::Count:
        break;
    }
    return "unknown";
}

// Length of `AdapterInfo::name` including the terminator.
inline constexpr u32 kAdapterNameBytes = 128;

// One physical or emulated adapter. Trivially copyable: a caller may keep it,
// compare it, or log it without owning anything.
struct AdapterInfo
{
    // Human-readable adapter name, always null-terminated.
    char name[kAdapterNameBytes] = {};
    // PCI vendor and device identifiers, or 0 when the backend does not know.
    // The null backend reports 0.
    u32 vendorId = 0;
    u32 deviceId = 0;
    // Driver version as the backend reports it, or 0. Interpretation is
    // backend-specific and for logs only.
    u32 driverVersion = 0;
    // Device-local memory in bytes, or 0 when unknown.
    u64 dedicatedVideoMemoryBytes = 0;
    // Shared system memory usable by the adapter in bytes, or 0 when unknown.
    u64 sharedSystemMemoryBytes = 0;
    // Which API this adapter speaks.
    BackendKind backend = BackendKind::Null;
    // API version the backend targets. The null backend reports 0.
    u32 apiVersion = 0;
};

// Guaranteed maxima for one device. A caller reads these and plans; a backend
// that cannot honour a request fails the call rather than the frame.
//
// Every field is an inclusive maximum or a required alignment, never a
// suggestion. Phase 11 validation compares descriptors against them.
struct Limits
{
    // Largest texture edge per dimension, in texels.
    u32 maxTexture1D = 0;
    u32 maxTexture2D = 0;
    u32 maxTexture3D = 0;
    u32 maxTextureCube = 0;
    // Largest array layer count for 1D and 2D array textures.
    u32 maxTextureArrayLayers = 0;
    // Largest number of color attachments in one render pass.
    u32 maxColorAttachments = 0;
    // Largest number of vertex buffers and vertex attributes in one pipeline.
    u32 maxVertexBuffers = 0;
    u32 maxVertexAttributes = 0;
    // Largest number of binding groups a pipeline layout may declare, and of
    // bindings inside one group.
    u32 maxBindGroups = 0;
    u32 maxBindingsPerGroup = 0;
    // Largest push-constant block in bytes.
    u32 maxPushConstantBytes = 0;
    // Largest sampler anisotropy ratio. 1 means isotropic filtering only.
    u32 maxSamplerAnisotropy = 1;
    // Required alignment of a dynamic uniform buffer offset, in bytes.
    u32 minUniformBufferOffsetAlignment = 0;
    // Required alignment of a dynamic storage buffer offset, in bytes.
    u32 minStorageBufferOffsetAlignment = 0;
    // Largest single buffer in bytes.
    u64 maxBufferBytes = 0;
};

// Optional capabilities. A feature is present only when the adapter reports it;
// a caller that needs one must check before creating the object that depends on
// it and must handle `ErrorCode::Unsupported`.
//
// The list grows only when a phase's design note names the use: a closed enum
// means a misspelled or unimplemented feature is a compile error rather than a
// silently ignored string.
enum class Feature : u32
{
    // Compute pipelines and dispatch (Phase 12).
    Compute = 0,
    // Draw indexed or non-indexed with arguments in a GPU buffer.
    IndirectDraw,
    // Several indirect draws from one call.
    MultiDrawIndirect,
    // Fences carry a 64-bit value instead of a binary signal.
    TimelineFences,
    // Debug markers and object names the backend emits to a GPU debugger
    // (Phase 15).
    DebugMarkers,
    // Timestamp queries for pass timing (Phase 15).
    TimestampQueries,
    // A swapchain that may present with tearing instead of waiting for vsync
    // (Phase 13).
    SwapchainTearing,
    // Anisotropic texture filtering (Phase 11).
    AnisotropicFiltering,
    // Storage textures writable from a shader.
    StorageTextures,
    // Resource arrays addressed by a runtime index, without a binding per
    // resource (Phase 33).
    BindlessResources,
    // Geometry shaders. Optional because Metal and WebGPU do not have them.
    GeometryShaders,
    // Mesh shaders (Phase 33).
    MeshShaders,
    // Count of enumerators, for table tests. Not a feature.
    Count
};

// Enumerators `Feature` has, `Count` included.
inline constexpr u32 kFeatureCount = static_cast<u32>(Feature::Count);
static_assert(kFeatureCount <= 32, "Features bitset holds 32 flags");

// Human-readable feature name, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* FeatureName(Feature feature) noexcept
{
    switch (feature)
    {
    case Feature::Compute:
        return "compute";
    case Feature::IndirectDraw:
        return "indirect_draw";
    case Feature::MultiDrawIndirect:
        return "multi_draw_indirect";
    case Feature::TimelineFences:
        return "timeline_fences";
    case Feature::DebugMarkers:
        return "debug_markers";
    case Feature::TimestampQueries:
        return "timestamp_queries";
    case Feature::SwapchainTearing:
        return "swapchain_tearing";
    case Feature::AnisotropicFiltering:
        return "anisotropic_filtering";
    case Feature::StorageTextures:
        return "storage_textures";
    case Feature::BindlessResources:
        return "bindless_resources";
    case Feature::GeometryShaders:
        return "geometry_shaders";
    case Feature::MeshShaders:
        return "mesh_shaders";
    case Feature::Count:
        break;
    }
    return "unknown";
}

// Set of reported features. 32 bits, trivially copyable, so it can be stored in
// a `Capabilities` value without allocating.
struct Features
{
    u32 bits = 0;

    // A set containing exactly one feature.
    [[nodiscard]] static constexpr Features Of(Feature feature) noexcept
    {
        Features result;
        result.Add(feature);
        return result;
    }

    // True when the set contains `feature`. `Feature::Count` and out-of-range
    // values are never present.
    [[nodiscard]] constexpr bool Has(Feature feature) const noexcept
    {
        const u32 index = static_cast<u32>(feature);
        if (index >= kFeatureCount)
        {
            return false;
        }
        return (bits & (1U << index)) != 0;
    }

    // Adds one feature. Adding an already present feature is a no-op.
    constexpr void Add(Feature feature) noexcept
    {
        const u32 index = static_cast<u32>(feature);
        if (index < kFeatureCount)
        {
            bits |= (1U << index);
        }
    }

    // Removes one feature. Removing an absent feature is a no-op.
    constexpr void Remove(Feature feature) noexcept
    {
        const u32 index = static_cast<u32>(feature);
        if (index < kFeatureCount)
        {
            bits &= ~(1U << index);
        }
    }

    friend constexpr bool operator==(Features lhs, Features rhs) noexcept
    {
        return lhs.bits == rhs.bits;
    }

    friend constexpr bool operator!=(Features lhs, Features rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend constexpr Features operator|(Features lhs, Feature rhs) noexcept
    {
        lhs.Add(rhs);
        return lhs;
    }

    friend constexpr Features operator|(Features lhs, Features rhs) noexcept
    {
        lhs.bits |= rhs.bits;
        return lhs;
    }
};

// What a device guarantees. A value type: cheap to copy, safe to store, and
// safe to read from any thread.
struct Capabilities
{
    AdapterInfo adapter{};
    Limits limits{};
    Features features{};
};

}  // namespace cyber::rhi
