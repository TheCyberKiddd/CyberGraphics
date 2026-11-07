#pragma once

#include <cyber/assets/asset.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>

#include <utility>

namespace cyber::assets
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::Span;
using cyber::memory::String;
using cyber::memory::StringView;

/// Version of the manifest layout. Independent of the cooked payload version:
/// the database outlives a payload format bump, because a version mismatch only
/// makes the caller re-cook.
inline constexpr u32 kAssetDatabaseVersion = 1;

/// One source asset and everything the cooker knows about it. Records are owned
/// by the database; pointers returned from it stay valid until the database is
/// destroyed or reloaded.
class AssetRecord
{
public:
    explicit AssetRecord(Allocator& allocator) noexcept
        : virtualPath(allocator), cookedPath(allocator), dependencies(allocator)
    {
    }

    AssetRecord(const AssetRecord&) = delete;
    AssetRecord& operator=(const AssetRecord&) = delete;
    AssetRecord(AssetRecord&&) noexcept = default;
    AssetRecord& operator=(AssetRecord&&) noexcept = default;
    ~AssetRecord() = default;

    AssetId id = kInvalidAssetId;
    /// Normalized path relative to the content root, always using '/'.
    String virtualPath;
    SourceFormat format = SourceFormat::Unknown;
    AssetKind kind = AssetKind::Unknown;
    /// xxHash64 of the source bytes the cooked payload was built from.
    u64 sourceHash = 0;
    /// ImportSettingsHash of the settings the cooked payload was built with.
    u64 settingsHash = 0;
    /// Cooked file path relative to the output root; empty when not cooked.
    String cookedPath;
    /// Payload checksum of the cooked file, or 0 when not cooked.
    u64 cookedChecksum = 0;
    /// Assets this one references. They cook first and invalidate with it.
    DynamicArray<AssetId> dependencies;

    [[nodiscard]] bool IsCooked() const noexcept
    {
        return !cookedPath.Empty();
    }
};

/// A cache hit. The three fields are the values the cooked payload was built
/// from, so a hit can be logged and compared without touching the record again.
struct CookCacheEntry
{
    u64 sourceHash = 0;
    u64 settingsHash = 0;
    u64 checksum = 0;
};

/// Registry of source assets plus the cook cache built from them. The database
/// never touches the file system while registering or caching: callers read,
/// cook, and write bytes, then report results here. That keeps it usable from
/// the offline cooker, the runtime, and tests without a shared working
/// directory; only Save and Load touch a path.
class AssetDatabase
{
public:
    explicit AssetDatabase(Allocator& allocator) noexcept
        : m_allocator(allocator), m_records(allocator)
    {
    }

    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;
    AssetDatabase(AssetDatabase&&) noexcept = default;
    AssetDatabase& operator=(AssetDatabase&&) noexcept = delete;
    ~AssetDatabase() = default;

    /// Registers an asset under a virtual path. The path is normalized: '\\'
    /// becomes '/', duplicate separators collapse, and leading "./" or "/" is
    /// removed. Segments equal to `.` are dropped and a `..` segment is rejected
    /// with `InvalidArgument`, because an asset path may never escape the content
    /// root. Re-registering the same path returns the existing id; registering it
    /// with a different format or an unknown extension also fails with
    /// `InvalidArgument`.
    [[nodiscard]] ImportResult<AssetId> Register(const StringView& virtualPath,
                                                 SourceFormat format) noexcept;

    /// Registers an asset, inferring the format from the extension.
    [[nodiscard]] ImportResult<AssetId> Register(const StringView& virtualPath) noexcept;

    /// Registers an asset the cooker produced instead of read from disk, such
    /// as a mesh extracted from a glTF scene. Derived assets have no source
    /// container (`SourceFormat::Unknown`) and carry an explicit kind, so the
    /// manifest still describes what their cooked payload contains.
    [[nodiscard]] ImportResult<AssetId> RegisterDerived(const StringView& virtualPath,
                                                        AssetKind kind) noexcept;

    [[nodiscard]] usize Count() const noexcept
    {
        return m_records.Size();
    }

    [[nodiscard]] const AssetRecord* Find(AssetId id) const noexcept;
    [[nodiscard]] const AssetRecord* Find(const StringView& virtualPath) const noexcept;
    [[nodiscard]] const AssetRecord* At(usize index) const noexcept;

    /// Declares that `asset` references `dependency`. A duplicate edge is a
    /// no-op. A missing asset or an edge that would create a cycle is
    /// `InvalidArgument`.
    [[nodiscard]] ImportStatus AddDependency(AssetId asset, AssetId dependency) noexcept;

    /// Replaces the dependency list of `asset`, rejecting cycles.
    [[nodiscard]] ImportStatus SetDependencies(AssetId asset,
                                               Span<const AssetId> dependencies) noexcept;

    /// True when `asset` references `candidate` directly or through a chain.
    [[nodiscard]] bool DependsOn(AssetId asset, AssetId candidate) const noexcept;

    /// Records a successful cook and its cache key. Replaces any previous entry
    /// for the asset, so only one payload per asset is remembered.
    [[nodiscard]] ImportStatus StoreCooked(AssetId asset, const char* cookedPath, u64 sourceHash,
                                           const AssetImportSettings& settings,
                                           u64 payloadChecksum) noexcept;

    /// Looks up a cooked payload matching the exact source bytes and settings.
    /// A hit does not prove the file still exists; the loader checks that.
    [[nodiscard]] bool TryGetCooked(AssetId asset, u64 sourceHash,
                                    const AssetImportSettings& settings,
                                    CookCacheEntry& entry) const noexcept;

    /// Forgets the cooked payload of `asset` and of every asset that references
    /// it, so a dependency change cannot leave a stale consumer behind.
    void Invalidate(AssetId asset) noexcept;

    /// Produces a cook order in which every dependency precedes its consumers.
    /// Returns false when the dependency graph has a cycle.
    [[nodiscard]] bool CookOrder(DynamicArray<AssetId>& order) const noexcept;

    /// Writes the manifest as JSON. Hashes are hex strings, so a u64 never loses
    /// precision to a double.
    [[nodiscard]] ImportStatus Save(const char* utf8Path) const noexcept;

    /// Loads a manifest written by Save. A different kAssetDatabaseVersion, a
    /// malformed document, or a dependency that is not registered is reported
    /// rather than guessed at. The database is cleared first.
    [[nodiscard]] ImportStatus Load(const char* utf8Path) noexcept;

    /// Removes every record, leaving the allocator in place.
    void Clear() noexcept;

private:
    enum class PathStatus : u8
    {
        Ok = 0,
        Empty,
        EscapesRoot,
        OutOfMemory,
    };

    [[nodiscard]] AssetRecord* FindMutable(AssetId id) noexcept;
    [[nodiscard]] static PathStatus NormalizePath(StringView source, String& out) noexcept;
    [[nodiscard]] bool WouldCycle(AssetId asset, AssetId dependency) const noexcept;

    Allocator& m_allocator;
    DynamicArray<AssetRecord> m_records;
};

}  // namespace cyber::assets
