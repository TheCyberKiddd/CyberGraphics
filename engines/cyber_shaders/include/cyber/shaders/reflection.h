#pragma once

#include <cyber/core/types.h>
#include <cyber/memory/string_view.h>

#include <array>

namespace cyber::shaders
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;

/// Shader stage represented by the Cyber shader language.
enum class Stage : u8
{
    Vertex = 0,
    Pixel,
    Compute,
};

/// Return a human-readable name for a shader stage.
///
/// @param stage Stage to name.
/// @return A static-lifetime name, or "unknown" for an invalid enumerator.
[[nodiscard]] const char* StageName(Stage stage) noexcept;

/// Longest identifier, in bytes, that the language accepts for a declared name.
inline constexpr u32 kMaxNameLength = 63;
/// Most keywords one shader may declare; the permutation space is 2^8 = 256.
inline constexpr u32 kMaxKeywords = 8;
/// Most stage inputs or outputs one shader may declare.
inline constexpr u32 kMaxStageVariables = 16;
/// Most bound resources (constant buffers, textures, samplers) one shader may declare.
inline constexpr u32 kMaxResources = 32;
/// Most constant-buffer members one shader may declare.
inline constexpr u32 kMaxConstantMembers = 32;

/// A name copied out of the source so reflection never borrows source text.
/// Trivially copyable; `size` is at most kMaxNameLength and `text` is
/// zero-terminated.
struct ReflectedName
{
    std::array<char, kMaxNameLength + 1> text{};
    u8 size = 0;

    /// Return a view of the stored name.
    ///
    /// @return A non-owning view, valid while this object is alive.
    [[nodiscard]] memory::StringView View() const noexcept
    {
        return memory::StringView(text.data(), size);
    }
};

/// A value type that can cross a stage boundary or live in a constant buffer.
enum class ValueType : u8
{
    Float = 0,
    Float2,
    Float3,
    Float4,
    Float4x4,
    Uint3,
};

/// Return the tight byte size of a reflected value type.
///
/// @param type Type to measure.
/// @return Its size in bytes; zero for an invalid enumerator.
[[nodiscard]] u32 ValueTypeSize(ValueType type) noexcept;

/// Return the source-language spelling of a reflected value type.
///
/// @param type Type to name.
/// @return A static-lifetime spelling, or "unknown" for an invalid enumerator.
[[nodiscard]] const char* ValueTypeName(ValueType type) noexcept;

/// The system value a stage variable is bound to, if any.
enum class Builtin : u8
{
    None = 0,
    /// Clip-space position (`SV_Position`).
    Position,
    /// Compute dispatch thread identifier (`SV_DispatchThreadID`).
    DispatchThreadId,
};

/// One stage input or output. `location` is meaningful only when `builtin` is
/// Builtin::None.
struct StageVariable
{
    ReflectedName name{};
    ValueType type = ValueType::Float;
    Builtin builtin = Builtin::None;
    u32 location = 0;
};

/// What a bound resource is.
enum class ResourceKind : u8
{
    /// A constant buffer (`b` register). Its members are in Reflection::members.
    ConstantBuffer = 0,
    /// A sampled 2D texture (`t` register).
    SampledTexture,
    /// A sampler (`s` register).
    Sampler,
    /// A read-write 2D storage texture (`u` register).
    StorageTexture,
};

/// Return a human-readable name for a resource kind.
///
/// @param kind Resource kind to name.
/// @return A static-lifetime name, or "unknown" for an invalid enumerator.
[[nodiscard]] const char* ResourceKindName(ResourceKind kind) noexcept;

/// One bound resource as the generated HLSL declares it.
///
/// For a constant buffer, `name` is the generated cbuffer name, `sizeBytes` is
/// the 16-byte-rounded buffer size, and `firstMember`/`memberCount` index
/// Reflection::members. For other kinds those three fields are zero.
struct ResourceBinding
{
    ReflectedName name{};
    ResourceKind kind = ResourceKind::ConstantBuffer;
    u32 registerIndex = 0;
    u32 registerSpace = 0;
    u32 sizeBytes = 0;
    u32 firstMember = 0;
    u32 memberCount = 0;
};

/// One constant-buffer member, laid out with the HLSL packing rules: 4-byte
/// alignment, no value straddles a 16-byte register, and a matrix starts a new
/// register. The generated HLSL pins the same offset with `packoffset`.
struct ConstantMember
{
    ReflectedName name{};
    ValueType type = ValueType::Float;
    u32 offsetBytes = 0;
    u32 sizeBytes = 0;
};

/// Everything a renderer needs to bind a compiled shader, derived from the
/// typed program rather than from backend output. Trivially copyable.
struct Reflection
{
    Stage stage = Stage::Vertex;
    /// Compute thread-group size; all zero for graphics stages.
    std::array<u32, 3> threadGroupSize{};
    u32 inputCount = 0;
    u32 outputCount = 0;
    u32 resourceCount = 0;
    u32 memberCount = 0;
    u32 keywordCount = 0;
    std::array<StageVariable, kMaxStageVariables> inputs{};
    std::array<StageVariable, kMaxStageVariables> outputs{};
    std::array<ResourceBinding, kMaxResources> resources{};
    std::array<ConstantMember, kMaxConstantMembers> members{};
    /// Keywords the source declares, in declaration order.
    std::array<ReflectedName, kMaxKeywords> keywords{};
};

}  // namespace cyber::shaders
