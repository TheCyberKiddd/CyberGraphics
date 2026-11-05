#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/jobs/job_system.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>
#include <cyber/shaders/cache.h>
#include <cyber/shaders/compiler.h>

namespace cyber::shaders
{

using cyber::core::u32;
using cyber::core::u64;

/// Identifies a shader registered with a ShaderLibrary. Zero is invalid.
using ShaderId = u32;

/// Creation parameters for a ShaderLibrary.
struct ShaderLibraryDesc
{
    /// Job system that runs compilation. Borrowed; must outlive the library.
    /// Null compiles synchronously inside Update, which is meant for tools and
    /// tests, not for a render thread.
    jobs::JobSystem* jobs = nullptr;
    /// Optional on-disk cache. Borrowed; must outlive the library.
    ShaderCache* cache = nullptr;
    /// Cache target recorded in every key. Borrowed static string.
    const char* target = kHlslTarget;
    /// Most permutations one shader may request; at most 256 (2^kMaxKeywords).
    u32 maxPermutationsPerShader = 64;
    /// Minimum time between file-system polls in Update.
    u32 pollIntervalMilliseconds = 50;
    /// Watch registered files and recompile them when they change.
    bool watchFiles = true;
};

/// What happened to a shader during Update.
enum class ShaderEventKind : u32
{
    /// Every requested permutation compiled; the shader's version advanced.
    Compiled = 0,
    /// At least one permutation failed; the previous results are kept.
    Failed,
};

/// One published compilation outcome.
struct ShaderEvent
{
    ShaderId shader = 0;
    ShaderEventKind kind = ShaderEventKind::Compiled;
    /// Version after the event; unchanged by a failure.
    u32 version = 0;
    /// Nanoseconds from the detected change (or registration) to publication.
    u64 latencyNanoseconds = 0;
};

/// Per-shader counters for reports and tests.
struct ShaderStats
{
    Stage stage = Stage::Vertex;
    u32 declaredKeywords = 0;
    /// 2^declaredKeywords, the size of the permutation space.
    u32 possiblePermutations = 1;
    u32 requestedPermutations = 0;
    /// Permutations held by the current good version.
    u32 compiledPermutations = 0;
    u64 compilations = 0;
    u64 cacheHits = 0;
    u64 failures = 0;
    u32 version = 0;
};

/// Compiles registered shader files and their requested permutations on the
/// job system, caches the results, and recompiles files that change.
///
/// Threading: every member function must be called from the owning thread.
/// Compilation runs on job workers; finished results are published only by
/// Update, so results returned by Find stay valid until the next Update.
/// Update never blocks on compilation. All memory, including memory used by
/// workers, comes from the allocator passed to Create through an internal
/// lock, because memory::Allocator is not thread-safe.
///
/// Failure policy: a recompile publishes only when every requested permutation
/// compiles. Otherwise the previous version stays current, the first
/// diagnostic is kept for LastError, and a warning is logged.
class ShaderLibrary
{
public:
    /// Construct an empty library.
    ShaderLibrary() noexcept = default;
    /// Wait for outstanding worker jobs and release owned state.
    ~ShaderLibrary();

    /// Shader libraries are move-only.
    ShaderLibrary(const ShaderLibrary&) = delete;
    ShaderLibrary& operator=(const ShaderLibrary&) = delete;
    ShaderLibrary(ShaderLibrary&& other) noexcept;
    ShaderLibrary& operator=(ShaderLibrary&& other) noexcept;

    /// Create a library.
    ///
    /// @param desc Borrowed configuration; see ShaderLibraryDesc.
    /// @param allocator Backs all library state; must outlive the library.
    /// @return The library, or ErrorCode::InvalidArgument for a bad limit or
    ///         ErrorCode::Internal on allocation failure.
    [[nodiscard]] static core::Result<ShaderLibrary, core::Error>
    Create(const ShaderLibraryDesc& desc, memory::Allocator& allocator) noexcept;

    /// Register a shader file and request its keyword-free permutation. The
    /// first compilation starts at the next Update.
    ///
    /// @param utf8Path File path; copied.
    /// @return The shader's id, or ErrorCode::InvalidArgument for an empty
    ///         path, ErrorCode::AlreadyExists for a path already registered,
    ///         or ErrorCode::Internal on allocation failure.
    [[nodiscard]] core::Result<ShaderId, core::Error>
    Register(memory::StringView utf8Path) noexcept;

    /// Request one more permutation of `shader`. Requesting a permutation that
    /// is already requested is a no-op. Keywords are validated when the shader
    /// compiles; an undeclared keyword fails that compilation.
    ///
    /// @param shader A registered shader.
    /// @param keywords Enabled keyword names; copied. At most kMaxKeywords,
    ///        each at most kMaxNameLength bytes.
    /// @return The permutation hash to pass to Find, or ErrorCode::InvalidArgument
    ///         for a bad id or keyword, or ErrorCode::Overflow when the shader
    ///         already has maxPermutationsPerShader requests.
    [[nodiscard]] core::Result<u64, core::Error>
    RequestPermutation(ShaderId shader, memory::Span<const memory::StringView> keywords) noexcept;

    /// Request every permutation the shader declares, up to the limit. Applied
    /// at its next compilation, once its keywords are known.
    ///
    /// @return Ok, or ErrorCode::InvalidArgument for a bad id.
    [[nodiscard]] core::Status<core::Error> RequestAllPermutations(ShaderId shader) noexcept;

    /// Poll watched files (at most every pollIntervalMilliseconds), start
    /// recompiles for changed or new shaders, and publish finished work as
    /// events. Never waits for compilation unless the library has no job system.
    void Update() noexcept;

    /// Mark a shader dirty as if its file had changed.
    void Invalidate(ShaderId shader) noexcept;

    /// Wait until no compilation is in flight, then publish. For shutdown and
    /// tests; never call it on a render thread mid-frame.
    void WaitIdle() noexcept;

    /// Pop the next published event. Returns false when none is pending.
    [[nodiscard]] bool PollEvent(ShaderEvent& event) noexcept;

    /// Current good result for one permutation, or null when the shader has no
    /// successful version yet or the permutation is not compiled.
    ///
    /// @param shader A registered shader.
    /// @param permutationHash A hash returned by RequestPermutation, or
    ///        kPermutationHashSeed for the keyword-free permutation.
    /// @return A pointer owned by the library, valid until the next Update,
    ///         WaitIdle, or destruction.
    [[nodiscard]] const CompiledShader* Find(ShaderId shader, u64 permutationHash) const noexcept;

    /// Version of the shader's current good result; 0 before the first success.
    [[nodiscard]] u32 Version(ShaderId shader) const noexcept;

    /// The most recent compilation diagnostic, or null when the last
    /// compilation succeeded. `sourceName` borrows the library's path copy.
    [[nodiscard]] const Diagnostic* LastError(ShaderId shader) const noexcept;

    /// Counters for one shader; zeroed for a bad id.
    [[nodiscard]] ShaderStats Stats(ShaderId shader) const noexcept;

    /// The registered path of a shader, or an empty view for a bad id.
    [[nodiscard]] memory::StringView Path(ShaderId shader) const noexcept;

    /// Number of registered shaders. Ids run from 1 to ShaderCount().
    [[nodiscard]] u32 ShaderCount() const noexcept;

    /// Append a plain-text permutation report: one line per shader with its
    /// path, stage, keywords, possible, requested, and compiled permutations,
    /// compilations, cache hits, failures, and last error, plus totals.
    ///
    /// @return false when `out` cannot grow.
    [[nodiscard]] bool WritePermutationReport(memory::String& out) const noexcept;

private:
    struct Impl;
    explicit ShaderLibrary(Impl* impl) noexcept;
    Impl* m_impl = nullptr;
};

}  // namespace cyber::shaders
