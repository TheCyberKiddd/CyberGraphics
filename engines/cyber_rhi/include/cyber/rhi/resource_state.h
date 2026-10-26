#pragma once

#include <cyber/core/types.h>
#include <cyber/rhi/buffer.h>
#include <cyber/rhi/texture.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;

// How a resource is being read or written right now. The vocabulary is the RHI's
// own: a backend maps it to `D3D12_RESOURCE_STATES` or a `VkImageLayout`, and the
// mapping tables live in those backends, not here.
//
// The value is a bitmask because a resource may legitimately be read in two ways
// at once — a vertex buffer that a copy also reads is
// `VertexBuffer | CopySource`. Two rules keep it honest:
//
// - read states combine freely;
// - a set that contains a write state contains nothing else, because two
//   consumers of one write are a race a barrier cannot fix.
//
// `Undefined` means "contents may be discarded". It is legal only as the `from`
// side of a barrier, where it says the caller does not care what the resource
// held, and it is the only `from` value that bypasses the tracked-state check.
// It is never a legal `to` value.
enum class ResourceState : u32
{
    // Unknown or discarded contents. Legal only as a transition source.
    Undefined = 0,
    // Held for presentation by the display engine. Swapchain images only.
    Present = 1U << 0,
    // Read by a copy command.
    CopySource = 1U << 1,
    // Written by a copy command.
    CopyDestination = 1U << 2,
    // Read as vertex input.
    VertexBuffer = 1U << 3,
    // Read as index input.
    IndexBuffer = 1U << 4,
    // Read as constant data.
    ConstantBuffer = 1U << 5,
    // Read as a shader resource: a sampled texture or a read-only buffer.
    ShaderRead = 1U << 6,
    // Written as a storage texture or a storage buffer.
    ShaderWrite = 1U << 7,
    // Written as a color attachment.
    RenderTarget = 1U << 8,
    // Read as a depth attachment that does not write depth.
    DepthRead = 1U << 9,
    // Written as a depth attachment. Implies reading for the depth test.
    DepthWrite = 1U << 10,
    // Read as indirect draw or dispatch arguments.
    IndirectArgument = 1U << 11,
};

// Bits of every read state.
inline constexpr u32 kResourceStateReadMask =
    static_cast<u32>(ResourceState::CopySource) | static_cast<u32>(ResourceState::VertexBuffer) |
    static_cast<u32>(ResourceState::IndexBuffer) | static_cast<u32>(ResourceState::ConstantBuffer) |
    static_cast<u32>(ResourceState::ShaderRead) | static_cast<u32>(ResourceState::DepthRead) |
    static_cast<u32>(ResourceState::IndirectArgument);

// Bits of every write state.
inline constexpr u32 kResourceStateWriteMask =
    static_cast<u32>(ResourceState::Present) | static_cast<u32>(ResourceState::CopyDestination) |
    static_cast<u32>(ResourceState::ShaderWrite) | static_cast<u32>(ResourceState::RenderTarget) |
    static_cast<u32>(ResourceState::DepthWrite);

// Set union.
[[nodiscard]] constexpr ResourceState operator|(ResourceState lhs, ResourceState rhs) noexcept
{
    return static_cast<ResourceState>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

// Set intersection.
[[nodiscard]] constexpr ResourceState operator&(ResourceState lhs, ResourceState rhs) noexcept
{
    return static_cast<ResourceState>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
}

// Set union in place.
constexpr ResourceState& operator|=(ResourceState& lhs, ResourceState rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

// The raw bits, for serialization and logging.
[[nodiscard]] constexpr u32 ResourceStateBits(ResourceState state) noexcept
{
    return static_cast<u32>(state);
}

// True when `set` contains every bit of `state`. A zero `state` is never
// contained.
[[nodiscard]] constexpr bool HasState(ResourceState set, ResourceState state) noexcept
{
    return state != ResourceState::Undefined &&
           (ResourceStateBits(set) & ResourceStateBits(state)) == ResourceStateBits(state);
}

// True when the set contains at least one read state and no write state.
[[nodiscard]] constexpr bool IsReadState(ResourceState state) noexcept
{
    const u32 bits = ResourceStateBits(state);
    return (bits & kResourceStateReadMask) != 0 && (bits & kResourceStateWriteMask) == 0;
}

// True when the set contains a write state.
[[nodiscard]] constexpr bool IsWriteState(ResourceState state) noexcept
{
    return (ResourceStateBits(state) & kResourceStateWriteMask) != 0;
}

// True when the set is a state a resource may actually be put into: at least one
// state, and either read-only or exactly one write state. `Undefined` is false,
// so this is the check the `to` side of a barrier uses.
[[nodiscard]] constexpr bool IsValidStateSet(ResourceState state) noexcept
{
    const u32 bits = ResourceStateBits(state);
    if (bits == 0)
    {
        return false;
    }
    const u32 writes = bits & kResourceStateWriteMask;
    if (writes == 0)
    {
        return true;
    }
    // A single write bit, and nothing else.
    return (writes & (writes - 1)) == 0 && bits == writes;
}

// True when every bit of the set is a state a buffer may be in.
[[nodiscard]] constexpr bool IsBufferResourceState(ResourceState state) noexcept
{
    constexpr u32 kAllowed = static_cast<u32>(ResourceState::CopySource) |
                             static_cast<u32>(ResourceState::CopyDestination) |
                             static_cast<u32>(ResourceState::VertexBuffer) |
                             static_cast<u32>(ResourceState::IndexBuffer) |
                             static_cast<u32>(ResourceState::ConstantBuffer) |
                             static_cast<u32>(ResourceState::ShaderRead) |
                             static_cast<u32>(ResourceState::ShaderWrite) |
                             static_cast<u32>(ResourceState::IndirectArgument);
    const u32 bits = ResourceStateBits(state);
    return bits != 0 && (bits & ~kAllowed) == 0;
}

// True when every bit of the set is a state a texture may be in.
[[nodiscard]] constexpr bool IsTextureResourceState(ResourceState state) noexcept
{
    constexpr u32 kAllowed =
        static_cast<u32>(ResourceState::Present) | static_cast<u32>(ResourceState::CopySource) |
        static_cast<u32>(ResourceState::CopyDestination) |
        static_cast<u32>(ResourceState::ShaderRead) | static_cast<u32>(ResourceState::ShaderWrite) |
        static_cast<u32>(ResourceState::RenderTarget) | static_cast<u32>(ResourceState::DepthRead) |
        static_cast<u32>(ResourceState::DepthWrite);
    const u32 bits = ResourceStateBits(state);
    return bits != 0 && (bits & ~kAllowed) == 0;
}

// Human-readable name for a single state, "multiple" for a combination, or
// "undefined". The string has static lifetime.
[[nodiscard]] constexpr const char* ResourceStateName(ResourceState state) noexcept
{
    switch (state)
    {
    case ResourceState::Undefined:
        return "undefined";
    case ResourceState::Present:
        return "present";
    case ResourceState::CopySource:
        return "copy_source";
    case ResourceState::CopyDestination:
        return "copy_destination";
    case ResourceState::VertexBuffer:
        return "vertex_buffer";
    case ResourceState::IndexBuffer:
        return "index_buffer";
    case ResourceState::ConstantBuffer:
        return "constant_buffer";
    case ResourceState::ShaderRead:
        return "shader_read";
    case ResourceState::ShaderWrite:
        return "shader_write";
    case ResourceState::RenderTarget:
        return "render_target";
    case ResourceState::DepthRead:
        return "depth_read";
    case ResourceState::DepthWrite:
        return "depth_write";
    case ResourceState::IndirectArgument:
        return "indirect_argument";
    }
    return "multiple";
}

// True when `aspect` addresses channels `format` actually stores.
[[nodiscard]] constexpr bool IsAspectValidForFormat(Format format, TextureAspect aspect) noexcept
{
    const FormatInfo info = FormatInfoOf(format);
    switch (aspect)
    {
    case TextureAspect::Color:
        return IsColorFormat(format);
    case TextureAspect::Depth:
        return info.depth;
    case TextureAspect::Stencil:
        return info.stencil;
    case TextureAspect::DepthStencil:
        return info.depth;
    case TextureAspect::Count:
        break;
    }
    return false;
}

// A `SubresourceRange` with its zero counts resolved against a texture shape.
struct ResolvedRange
{
    // False when the range named a level, layer, or aspect the texture does not
    // have. Every other field is zero in that case.
    bool valid = false;
    u32 baseMipLevel = 0;
    u32 mipLevelCount = 0;
    u32 baseArrayLayer = 0;
    u32 layerCount = 0;
    TextureAspect aspect = TextureAspect::Color;
};

// Resolves a range against `info`, turning a zero mip or layer count into "every
// remaining one" and rejecting a range that leaves the texture. Returns an
// invalid range rather than asserting, so a backend can report
// `ErrorCode::InvalidArgument`.
[[nodiscard]] constexpr ResolvedRange
ResolveSubresourceRange(const TextureInfo& info, const SubresourceRange& range) noexcept
{
    ResolvedRange resolved;
    if (!info.valid || info.mipLevels == 0 || info.arrayLayers == 0)
    {
        return resolved;
    }
    if (range.baseMipLevel >= info.mipLevels || range.baseArrayLayer >= info.arrayLayers)
    {
        return resolved;
    }
    const u32 remainingMips = info.mipLevels - range.baseMipLevel;
    const u32 remainingLayers = info.arrayLayers - range.baseArrayLayer;
    const u32 mipCount = range.mipLevelCount == 0 ? remainingMips : range.mipLevelCount;
    const u32 layerCount = range.layerCount == 0 ? remainingLayers : range.layerCount;
    if (mipCount > remainingMips || layerCount > remainingLayers)
    {
        return resolved;
    }
    if (!IsAspectValidForFormat(info.format, range.aspect))
    {
        return resolved;
    }

    resolved.valid = true;
    resolved.baseMipLevel = range.baseMipLevel;
    resolved.mipLevelCount = mipCount;
    resolved.baseArrayLayer = range.baseArrayLayer;
    resolved.layerCount = layerCount;
    resolved.aspect = range.aspect;
    return resolved;
}

// A transition of one buffer. `from` must equal the tracked state or be
// `Undefined`; `to` must be a valid buffer state set.
struct BufferBarrier
{
    BufferHandle buffer;
    ResourceState from = ResourceState::Undefined;
    ResourceState to = ResourceState::Undefined;
};

// A transition of a rectangular subresource window of one texture. Every
// subresource in the range transitions together.
struct TextureBarrier
{
    TextureHandle texture;
    SubresourceRange range{};
    ResourceState from = ResourceState::Undefined;
    ResourceState to = ResourceState::Undefined;
};

}  // namespace cyber::rhi
