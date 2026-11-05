#pragma once

#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>

namespace cyber::shaders
{

using cyber::core::u32;
using cyber::core::u8;

/// Shader stage represented by the Phase 16 source language.
enum class Stage : u8
{
    Vertex = 0,
    Pixel,
};

/// Category of a shader source or code-generation diagnostic.
enum class DiagnosticKind : u8
{
    Lexical = 0,
    Syntax,
    Type,
    Unsupported,
    OutOfMemory,
};

/// A compiler diagnostic. `sourceName` borrows the name passed to CompileHlsl;
/// `message` points to a static string.
struct Diagnostic
{
    DiagnosticKind kind = DiagnosticKind::Syntax;
    cyber::memory::StringView sourceName{};
    u32 line = 1;
    u32 column = 1;
    const char* message = nullptr;
};

/// Generated HLSL owned by the caller's allocator.
struct CompiledShader
{
    Stage stage = Stage::Vertex;
    cyber::memory::String hlsl;
};

/// Compile one Cyber shader source unit into HLSL.
///
/// @param sourceName Non-owning diagnostic name for the source unit.
/// @param source Non-owning UTF-8 source text.
/// @param allocator Allocator used for compiler scratch storage and generated
///        HLSL; it must outlive the returned CompiledShader.
/// @return HLSL and its stage on success, or a diagnostic with a one-based
///         source location on failure. No partial output is returned.
[[nodiscard]] cyber::core::Result<CompiledShader, Diagnostic>
CompileHlsl(cyber::memory::StringView sourceName, cyber::memory::StringView source,
            cyber::memory::Allocator& allocator) noexcept;

}  // namespace cyber::shaders
