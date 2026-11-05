#pragma once

#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>
#include <cyber/shaders/permutation.h>
#include <cyber/shaders/reflection.h>

namespace cyber::shaders
{

using cyber::core::u32;
using cyber::core::u8;

/// Version of the compiler front end and HLSL generator. Any change that can
/// alter generated HLSL or reflection must increment it; it is part of every
/// shader cache key so stale entries are never reused after an upgrade.
inline constexpr u32 kCompilerVersion = 2;

/// Category of a shader source or code-generation diagnostic.
enum class DiagnosticKind : u8
{
    Lexical = 0,
    Syntax,
    Type,
    Unsupported,
    OutOfMemory,
    /// A requested permutation names an undeclared keyword.
    Permutation,
};

/// A compiler diagnostic. `sourceName` borrows the name passed to the compiler;
/// `message` points to a static string.
struct Diagnostic
{
    DiagnosticKind kind = DiagnosticKind::Syntax;
    cyber::memory::StringView sourceName{};
    u32 line = 1;
    u32 column = 1;
    const char* message = nullptr;
};

/// Per-compilation choices.
struct CompileOptions
{
    /// Keywords enabled for this permutation. Borrowed for the duration of the
    /// call. Repeated names count once; an undeclared name is a
    /// DiagnosticKind::Permutation error.
    cyber::memory::Span<const cyber::memory::StringView> keywords{};
};

/// Generated HLSL owned by the caller's allocator, plus its reflection.
struct CompiledShader
{
    Stage stage = Stage::Vertex;
    cyber::memory::String hlsl;
    Reflection reflection{};
    PermutationKey permutation{};
};

/// Compile one Cyber shader source unit into HLSL with no keywords enabled.
///
/// @param sourceName Non-owning diagnostic name for the source unit.
/// @param source Non-owning UTF-8 source text.
/// @param allocator Allocator used for compiler scratch storage and generated
///        HLSL; it must outlive the returned CompiledShader.
/// @return HLSL, reflection, and the permutation key on success, or a
///         diagnostic with a one-based source location on failure. No partial
///         output is returned.
[[nodiscard]] cyber::core::Result<CompiledShader, Diagnostic>
CompileHlsl(cyber::memory::StringView sourceName, cyber::memory::StringView source,
            cyber::memory::Allocator& allocator) noexcept;

/// Compile one permutation of a Cyber shader source unit into HLSL.
///
/// Every `when` branch is lexed and parsed whatever the permutation, so a
/// syntax error anywhere in the source fails every permutation. Only the
/// selected branches are type-checked, reflected, and emitted.
///
/// @param sourceName Non-owning diagnostic name for the source unit.
/// @param source Non-owning UTF-8 source text.
/// @param options Enabled keywords; borrowed for the call.
/// @param allocator Scratch and output allocator; must outlive the result.
/// @return The compiled permutation or a diagnostic. No partial output.
[[nodiscard]] cyber::core::Result<CompiledShader, Diagnostic>
CompileHlsl(cyber::memory::StringView sourceName, cyber::memory::StringView source,
            const CompileOptions& options, cyber::memory::Allocator& allocator) noexcept;

/// The keywords a source declares, in declaration order.
struct KeywordList
{
    Stage stage = Stage::Vertex;
    u32 count = 0;
    std::array<ReflectedName, kMaxKeywords> names{};
};

/// Lex and parse `source` and list its stage and keywords without type
/// checking or emitting code.
///
/// @param sourceName Non-owning diagnostic name.
/// @param source Non-owning UTF-8 source text.
/// @param allocator Scratch allocator; nothing it allocates outlives the call.
/// @return The keyword list, or the first lexical or syntax diagnostic.
[[nodiscard]] cyber::core::Result<KeywordList, Diagnostic>
ScanKeywords(cyber::memory::StringView sourceName, cyber::memory::StringView source,
             cyber::memory::Allocator& allocator) noexcept;

}  // namespace cyber::shaders
