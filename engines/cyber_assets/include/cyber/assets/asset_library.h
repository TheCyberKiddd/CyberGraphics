#pragma once

#include <cyber/assets/cooked.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/string_view.h>

namespace cyber::assets
{

using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::StringView;

/// Runtime owner of cooked assets with hot reload. Each mounted asset keeps its
/// bytes alive and is re-read when the file on disk changes, so a cook cycle
/// does not require restarting the host. The library never decodes source
/// files; it only verifies container headers, so a reload is cheap and a broken
/// payload is rejected without taking the working asset away.
///
/// Views handed out by Find alias library-owned storage. They stay valid until
/// that asset is reloaded or the library is cleared, and `Generation()` changes
/// whenever any asset was replaced, which is the signal a renderer needs to
/// rebuild its bindings.
class AssetLibrary
{
public:
    /// Counts from one Poll cycle. A failed reload keeps the previous asset, so
    /// `failed` is a diagnostic and not a state change.
    struct ReloadStats
    {
        u32 reloaded = 0;
        u32 failed = 0;
    };

    explicit AssetLibrary(Allocator& allocator) noexcept;
    ~AssetLibrary();

    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;
    AssetLibrary(AssetLibrary&& other) noexcept;
    AssetLibrary& operator=(AssetLibrary&& other) noexcept;

    /// Loads a cooked file and starts watching it. `virtualPath` identifies the
    /// asset; mounting the same path twice is `AlreadyExists`. Returns the slot
    /// index, which is stable until Clear.
    [[nodiscard]] ImportResult<u32> Mount(const StringView& virtualPath,
                                          const char* cookedPath) noexcept;

    /// Mounted asset by virtual path, or null when it is not mounted.
    [[nodiscard]] const CookedAsset* Find(const StringView& virtualPath) const noexcept;

    /// Mounted asset by slot index, or null when out of range.
    [[nodiscard]] const CookedAsset* At(usize index) const noexcept;

    /// Virtual path of a slot, or an empty view when out of range.
    [[nodiscard]] StringView PathAt(usize index) const noexcept;

    [[nodiscard]] usize Count() const noexcept;

    /// Re-reads one asset. The old payload stays in place when the new bytes
    /// fail to load, parse, or verify.
    [[nodiscard]] ImportStatus Reload(const StringView& virtualPath) noexcept;

    /// Polls every watch and reloads the assets whose files changed.
    [[nodiscard]] ReloadStats Poll() noexcept;

    /// Number of changes applied to loaded assets since construction. Bumped
    /// only by successful reloads, so a caller can skip work when it is
    /// unchanged.
    [[nodiscard]] u64 Generation() const noexcept;

    /// Drops every mounted asset and watch. Generation keeps its value, because
    /// it is a mutation counter, not a content identity.
    void Clear() noexcept;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace cyber::assets
