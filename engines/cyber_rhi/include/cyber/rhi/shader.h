#pragma once

#include <cyber/core/types.h>
#include <cyber/memory/span.h>

namespace cyber::rhi
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

// A programmable stage. The list is the closed set every backend in this
// project implements; geometry and mesh stages arrive with the features that
// need them.
enum class ShaderStage : u8
{
    Vertex = 0,
    // The pixel stage. Named for D3D12; Vulkan and Metal call it fragment.
    Pixel,
    Compute,
    // Count of enumerators, for table tests. Not a stage.
    Count
};

// Number of real stages, not counting `Count`.
inline constexpr u32 kShaderStageCount = static_cast<u32>(ShaderStage::Count);

// Bitset of stages a binding is visible to. A `u32` rather than a class so a
// descriptor stays trivially copyable and hashable.
using ShaderStageMask = u32;

// The bit `stage` occupies in a `ShaderStageMask`, or 0 for an invalid stage.
[[nodiscard]] constexpr ShaderStageMask ShaderStageBit(ShaderStage stage) noexcept
{
    const u32 index = static_cast<u32>(stage);
    return index < kShaderStageCount ? (1U << index) : 0U;
}

// Every stage at once.
inline constexpr ShaderStageMask kAllShaderStages = (1U << kShaderStageCount) - 1U;

// True when `mask` includes `stage`. An invalid stage is never included.
[[nodiscard]] constexpr bool HasStage(ShaderStageMask mask, ShaderStage stage) noexcept
{
    const ShaderStageMask bit = ShaderStageBit(stage);
    return bit != 0 && (mask & bit) != 0;
}

// Human-readable stage, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* ShaderStageName(ShaderStage stage) noexcept
{
    switch (stage)
    {
    case ShaderStage::Vertex:
        return "vertex";
    case ShaderStage::Pixel:
        return "pixel";
    case ShaderStage::Compute:
        return "compute";
    case ShaderStage::Count:
        break;
    }
    return "unknown";
}

// What the bytes in a shader module are.
enum class ShaderBytecodeFormat : u8
{
    // Text consumed by a compiler the backend ships with or calls: HLSL source
    // compiled through d3dcompiler on Windows.
    HlslSource = 0,
    // SPIR-V binary for a Vulkan backend.
    SpirV,
    // DXIL binary, accepted when a caller already holds compiled bytecode.
    Dxil,
    // Metal Shading Language source.
    MslSource,
    // Count of enumerators, for table tests. Not a format.
    Count
};

// Number of real bytecode formats, not counting `Count`.
inline constexpr u32 kShaderBytecodeFormatCount = static_cast<u32>(ShaderBytecodeFormat::Count);

// Human-readable bytecode format, or "unknown". The string has static lifetime.
[[nodiscard]] constexpr const char* ShaderBytecodeFormatName(ShaderBytecodeFormat format) noexcept
{
    switch (format)
    {
    case ShaderBytecodeFormat::HlslSource:
        return "hlsl";
    case ShaderBytecodeFormat::SpirV:
        return "spirv";
    case ShaderBytecodeFormat::Dxil:
        return "dxil";
    case ShaderBytecodeFormat::MslSource:
        return "msl";
    case ShaderBytecodeFormat::Count:
        break;
    }
    return "unknown";
}

// Description of a shader module.
//
// The module borrows its code: the caller keeps the bytes alive for as long as
// the module exists. A backend compiles on creation (D3D12 with d3dcompiler, a
// Vulkan backend from SPIR-V directly) and keeps the result; the source is not
// needed after the call returns.
struct ShaderDesc
{
    // Stage the module implements.
    ShaderStage stage = ShaderStage::Vertex;
    // How the bytes are interpreted.
    ShaderBytecodeFormat format = ShaderBytecodeFormat::HlslSource;
    // The code. A null pointer or an empty blob is invalid.
    memory::Span<const memory::Byte> code{};
    // Function the stage starts at. Required for source formats, ignored for
    // binaries. Borrowed; the caller keeps it alive or passes null.
    const char* entryPoint = nullptr;
    // Optional name for logs and debuggers. Borrowed.
    const char* label = nullptr;
};

// What a created shader module reports about itself. `valid` is false for a
// stale handle, in which case every other field is zero.
struct ShaderInfo
{
    bool valid = false;
    ShaderStage stage = ShaderStage::Vertex;
    ShaderBytecodeFormat format = ShaderBytecodeFormat::HlslSource;
    u64 codeSize = 0;
};

}  // namespace cyber::rhi
