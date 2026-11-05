#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string_view.h>
#include <cyber/shaders/compiler.h>

#include <array>
#include <atomic>

namespace cyber::shaders
{

using cyber::core::u32;
using cyber::core::u64;

/// The cache target of generated HLSL for the D3D12 shader model 5.1 path.
inline constexpr char kHlslTarget[] = "hlsl-sm5.1";

/// Version of the on-disk cache file layout. Independent of kCompilerVersion.
inline constexpr u32 kShaderCacheFormatVersion = 1;

/// Longest cache directory path, in bytes, ShaderCache accepts.
inline constexpr u32 kMaxShaderCachePath = 480;

/// Everything that can change a cached result. Two keys are equal only when
/// every field is equal; the cache stores the whole key in each entry, so a
/// file-name hash collision is reported as a miss, never as a hit.
struct ShaderCacheKey
{
    /// xxHash64 of the exact source bytes.
    u64 sourceHash = 0;
    /// PermutationKey::hash of the compiled permutation.
    u64 permutationHash = kPermutationHashSeed;
    /// xxHash64 of the target name (for example kHlslTarget).
    u64 targetHash = 0;
    /// kCompilerVersion of the compiler that produced the entry.
    u32 compilerVersion = kCompilerVersion;

    friend constexpr bool operator==(const ShaderCacheKey&, const ShaderCacheKey&) = default;
};

/// Build the cache key for compiling source as one permutation for a target.
///
/// @param source Exact source bytes; borrowed.
/// @param permutation Permutation identity.
/// @param target Target name; borrowed.
/// @return A key including source, compiler, permutation, and target hashes.
[[nodiscard]] ShaderCacheKey MakeShaderCacheKey(memory::StringView source,
                                                const PermutationKey& permutation,
                                                memory::StringView target) noexcept;

/// Serialize a compiled shader (stage, permutation, HLSL, reflection) into the
/// cache payload layout, appending to `out`.
///
/// @return false when `out` cannot grow; `out` may then hold a partial payload.
[[nodiscard]] bool SerializeCompiledShader(const CompiledShader& shader,
                                           memory::DynamicArray<memory::Byte>& out) noexcept;

/// Parse a payload written by SerializeCompiledShader. The bytes are treated as
/// hostile: every count, size, and enumerator is range-checked, and trailing
/// bytes are rejected.
///
/// @param payload Non-owning payload bytes.
/// @param allocator Owns the returned HLSL string.
/// @return The shader, or ErrorCode::ParseError (malformed) or
///         ErrorCode::Internal (allocation failure).
[[nodiscard]] core::Result<CompiledShader, core::Error>
DeserializeCompiledShader(memory::Span<const memory::Byte> payload,
                          memory::Allocator& allocator) noexcept;

/// Counters a cache keeps. Updated atomically; read with ShaderCache::Stats.
struct ShaderCacheStats
{
    u64 hits = 0;
    u64 misses = 0;
    /// Entries that existed but were rejected: wrong magic, version, key,
    /// checksum, or a malformed payload. Counted in `misses` too.
    u64 rejected = 0;
    u64 stores = 0;
    u64 storeFailures = 0;
};

/// A directory of cached compiled shaders, one file per key.
///
/// Thread-safety: Load, Store, and Stats may be called from any number of
/// threads at once. The directory is fixed at Open. Two threads storing the
/// same key at once may tear the file; a torn file fails its checksum and is a
/// miss on the next Load, so correctness never depends on write atomicity.
class ShaderCache
{
public:
    /// Construct a closed, empty cache.
    ShaderCache() noexcept = default;

    /// Open (creating if needed) a cache directory.
    ///
    /// @param directory UTF-8 path; copied. At most kMaxShaderCachePath bytes.
    /// @return The cache, ErrorCode::InvalidArgument for an empty or too-long
    ///         path, or the platform error when the directory cannot be created.
    [[nodiscard]] static core::Result<ShaderCache, core::Error>
    Open(memory::StringView directory) noexcept;

    /// Shader caches own no persistent handles and can be moved.
    ShaderCache(const ShaderCache&) = delete;
    ShaderCache& operator=(const ShaderCache&) = delete;
    ShaderCache(ShaderCache&& other) noexcept;
    ShaderCache& operator=(ShaderCache&& other) noexcept;
    /// Release this cache object.
    ~ShaderCache() = default;

    /// Check whether Open has succeeded.
    ///
    /// @return True when this cache is open.
    [[nodiscard]] bool IsOpen() const noexcept
    {
        return m_pathSize != 0;
    }

    /// Load the entry for `key`.
    ///
    /// @param key The exact key to look up.
    /// @param allocator Owns the returned HLSL and scratch file bytes.
    /// @return The cached shader, or ErrorCode::NotFound for any miss (absent,
    ///         stale, corrupt, or unreadable). A miss is never fatal.
    [[nodiscard]] core::Result<CompiledShader, core::Error>
    Load(const ShaderCacheKey& key, memory::Allocator& allocator) noexcept;

    /// Write the entry for `key`, replacing any existing file.
    ///
    /// @param key The key the shader was compiled for.
    /// @param shader The compiled shader to persist; not modified.
    /// @param allocator Scratch allocator for the serialized bytes.
    /// @return Ok, or the serialization or platform write error.
    [[nodiscard]] core::Status<core::Error> Store(const ShaderCacheKey& key,
                                                  const CompiledShader& shader,
                                                  memory::Allocator& allocator) noexcept;

    /// Snapshot the cache hit, miss, and write counters.
    ///
    /// @return An atomic snapshot of all counters.
    [[nodiscard]] ShaderCacheStats Stats() const noexcept;

    /// UTF-8 path of the file that holds `key`, written zero-terminated into
    /// `buffer`.
    ///
    /// @return false when the cache is not open or `buffer` is too small.
    [[nodiscard]] bool EntryPath(const ShaderCacheKey& key, char* buffer,
                                 u32 bufferSize) const noexcept;

private:
    std::array<char, kMaxShaderCachePath + 1> m_path{};
    u32 m_pathSize = 0;
    std::atomic<u64> m_hits{0};
    std::atomic<u64> m_misses{0};
    std::atomic<u64> m_rejected{0};
    std::atomic<u64> m_stores{0};
    std::atomic<u64> m_storeFailures{0};
};

}  // namespace cyber::shaders
