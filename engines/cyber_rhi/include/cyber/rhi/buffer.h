#pragma once

#include <cyber/core/types.h>
#include <cyber/rhi/handle.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;

// What a buffer may be used for. A buffer is created with a fixed set: the set
// is validated once, and every later operation checks the set it needs.
//
// `HostVisible` and `Readback` describe who may touch the bytes from the CPU.
// They are mutually exclusive and cannot be combined with a shader or pipeline
// usage, which is the upload-heap / readback-heap rule D3D12 enforces and the
// only sane memory-property selection on Vulkan. A staging buffer is therefore
// `HostVisible | CopySource`, never `HostVisible | Vertex`.
enum class BufferUsage : u32
{
    // No usage. A buffer created with this set is invalid.
    None = 0,
    // Vertex input for a graphics pipeline.
    Vertex = 1U << 0,
    // Index input for an indexed draw.
    Index = 1U << 1,
    // Constant data read by a shader. Offsets later need
    // `Limits::minUniformBufferOffsetAlignment`.
    Uniform = 1U << 2,
    // Structured or raw storage read and written by a shader.
    Storage = 1U << 3,
    // Indirect draw or dispatch arguments.
    Indirect = 1U << 4,
    // Read by a copy command.
    CopySource = 1U << 5,
    // Written by a copy command.
    CopyDestination = 1U << 6,
    // Writable from the CPU through `Device::WriteBuffer`, readable by the GPU
    // through a copy. The CPU may write at any time the GPU is not reading.
    HostVisible = 1U << 7,
    // Written by the GPU through a copy, readable from the CPU through
    // `Device::ReadBuffer`. Cannot be bound to a pipeline.
    Readback = 1U << 8,
};

// Set union.
[[nodiscard]] constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) noexcept
{
    return static_cast<BufferUsage>(static_cast<u32>(lhs) | static_cast<u32>(rhs));
}

// Set intersection.
[[nodiscard]] constexpr BufferUsage operator&(BufferUsage lhs, BufferUsage rhs) noexcept
{
    return static_cast<BufferUsage>(static_cast<u32>(lhs) & static_cast<u32>(rhs));
}

// Set union in place.
constexpr BufferUsage& operator|=(BufferUsage& lhs, BufferUsage rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

// The raw bits, for serialization and logging.
[[nodiscard]] constexpr u32 BufferUsageBits(BufferUsage usage) noexcept
{
    return static_cast<u32>(usage);
}

// True when `set` contains `flag`. A zero `flag` is never contained.
[[nodiscard]] constexpr bool HasUsage(BufferUsage set, BufferUsage flag) noexcept
{
    return flag != BufferUsage::None && (set & flag) != BufferUsage::None;
}

// True when the set is empty.
[[nodiscard]] constexpr bool IsEmpty(BufferUsage usage) noexcept
{
    return usage == BufferUsage::None;
}

// Human-readable name for a single usage flag, or "unknown" for a combination
// or a zero value. The string has static lifetime.
[[nodiscard]] constexpr const char* BufferUsageName(BufferUsage usage) noexcept
{
    switch (usage)
    {
    case BufferUsage::Vertex:
        return "vertex";
    case BufferUsage::Index:
        return "index";
    case BufferUsage::Uniform:
        return "uniform";
    case BufferUsage::Storage:
        return "storage";
    case BufferUsage::Indirect:
        return "indirect";
    case BufferUsage::CopySource:
        return "copy_source";
    case BufferUsage::CopyDestination:
        return "copy_destination";
    case BufferUsage::HostVisible:
        return "host_visible";
    case BufferUsage::Readback:
        return "readback";
    case BufferUsage::None:
        break;
    }
    return "unknown";
}

// Description of a buffer to create. A zeroed descriptor is invalid: no usage
// and no bytes.
struct BufferDesc
{
    // What the buffer may be used for. A combination that no backend can create
    // is `ErrorCode::InvalidArgument`.
    BufferUsage usage = BufferUsage::None;
    // Size in bytes. Zero is invalid; a value above `Limits::maxBufferBytes` is
    // `ErrorCode::OutOfRange`.
    u64 sizeBytes = 0;
    // Stride of one element for a structured buffer, in bytes. Zero means the
    // buffer is raw. A non-zero value must be a multiple of 4 and at most 2048,
    // which is D3D12's `D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT` rule and Vulkan's
    // storage-buffer offset alignment on every target.
    u32 elementStride = 0;
    // Optional name for logs and debuggers. Borrowed; the caller keeps it alive
    // for the buffer's lifetime or passes null.
    const char* label = nullptr;
};

// What a created buffer reports about itself. `valid` is false for a stale
// handle, in which case every other field is zero.
struct BufferInfo
{
    bool valid = false;
    u64 sizeBytes = 0;
    u32 elementStride = 0;
    BufferUsage usage = BufferUsage::None;
};

}  // namespace cyber::rhi
