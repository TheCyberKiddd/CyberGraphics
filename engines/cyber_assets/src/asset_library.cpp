#include <cyber/assets/asset_library.h>
#include <cyber/platform/filesystem.h>

#include <memory>
#include <utility>

namespace cyber::assets
{
namespace
{

using cyber::core::ErrorCode;
using cyber::platform::FileChange;
using cyber::platform::FileWatcher;

struct MountedAsset
{
    explicit MountedAsset(Allocator& allocator) noexcept
        : virtualPath(allocator), cookedPath(allocator), file(allocator)
    {
    }

    MountedAsset(const MountedAsset&) = delete;
    MountedAsset& operator=(const MountedAsset&) = delete;
    MountedAsset(MountedAsset&&) noexcept = default;
    MountedAsset& operator=(MountedAsset&&) noexcept = default;
    ~MountedAsset() = default;

    cyber::memory::String virtualPath;
    cyber::memory::String cookedPath;
    CookedFile file;
    u32 watchId = 0;
};

[[nodiscard]] ImportStatus Failure(ErrorCode code, const char* message) noexcept
{
    return ImportStatus::Err(MakeError(code, message));
}

}  // namespace

struct AssetLibrary::Impl
{
    explicit Impl(Allocator& allocator) noexcept
        : allocator(&allocator), entries(allocator), watcher(allocator)
    {
    }

    [[nodiscard]] MountedAsset* FindMutable(const StringView& virtualPath) noexcept
    {
        const AssetId id = AssetIdFromPath(virtualPath);
        for (usize index = 0; index < entries.Size(); ++index)
        {
            if (AssetIdFromPath(entries[index].virtualPath.View()) == id)
            {
                return &entries[index];
            }
        }
        return nullptr;
    }

    [[nodiscard]] MountedAsset* FindByWatch(u32 watchId) noexcept
    {
        for (usize index = 0; index < entries.Size(); ++index)
        {
            if (entries[index].watchId == watchId)
            {
                return &entries[index];
            }
        }
        return nullptr;
    }

    /// Loads `cookedPath` into `target`, leaving the current payload untouched
    /// when anything about the new bytes is wrong.
    [[nodiscard]] ImportStatus LoadInto(MountedAsset& target, const char* cookedPath) noexcept
    {
        auto loaded = CookedFile::Load(cookedPath, *allocator);
        if (loaded.IsError())
        {
            return ImportStatus::Err(loaded.Error());
        }
        target.file = std::move(loaded.Value());
        // Reload passes the entry's own path back in, so skip the copy when it
        // is unchanged instead of assigning a string from its own buffer.
        if (!target.cookedPath.View().Equals(StringView(cookedPath)) &&
            !target.cookedPath.TryAssign(StringView(cookedPath)))
        {
            return Failure(ErrorCode::OutOfRange, "out of memory while mounting an asset");
        }
        return ImportStatus::Ok();
    }

    Allocator* allocator = nullptr;
    cyber::memory::DynamicArray<MountedAsset> entries;
    FileWatcher watcher;
    u64 generation = 0;
};

AssetLibrary::AssetLibrary(Allocator& allocator) noexcept
{
    void* memory = allocator.Allocate(sizeof(Impl), alignof(Impl));
    if (memory == nullptr)
    {
        return;
    }
    m_impl = static_cast<Impl*>(memory);
    std::construct_at(m_impl, allocator);
}

AssetLibrary::~AssetLibrary()
{
    if (m_impl == nullptr)
    {
        return;
    }
    Allocator* allocator = m_impl->allocator;
    std::destroy_at(m_impl);
    if (allocator != nullptr)
    {
        allocator->Deallocate(m_impl);
    }
    m_impl = nullptr;
}

AssetLibrary::AssetLibrary(AssetLibrary&& other) noexcept : m_impl(other.m_impl)
{
    other.m_impl = nullptr;
}

AssetLibrary& AssetLibrary::operator=(AssetLibrary&& other) noexcept
{
    if (this != &other)
    {
        std::swap(m_impl, other.m_impl);
    }
    return *this;
}

ImportResult<u32> AssetLibrary::Mount(const StringView& virtualPath,
                                      const char* cookedPath) noexcept
{
    if (m_impl == nullptr)
    {
        return ImportResult<u32>::Err(
            MakeError(ErrorCode::OutOfRange, "asset library is not initialized"));
    }
    if (virtualPath.Empty())
    {
        return ImportResult<u32>::Err(
            MakeError(ErrorCode::InvalidArgument, "asset library path is empty"));
    }
    if (cookedPath == nullptr || cookedPath[0] == '\0')
    {
        return ImportResult<u32>::Err(
            MakeError(ErrorCode::InvalidArgument, "cooked path is empty"));
    }
    if (m_impl->FindMutable(virtualPath) != nullptr)
    {
        return ImportResult<u32>::Err(
            MakeError(ErrorCode::AlreadyExists, "asset is already mounted"));
    }

    auto loaded = CookedFile::Load(cookedPath, *m_impl->allocator);
    if (loaded.IsError())
    {
        return ImportResult<u32>::Err(loaded.Error());
    }

    MountedAsset* entry = m_impl->entries.TryEmplaceBack(*m_impl->allocator);
    if (entry == nullptr)
    {
        return ImportResult<u32>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while mounting an asset"));
    }
    if (!entry->virtualPath.TryAssign(virtualPath) ||
        !entry->cookedPath.TryAssign(StringView(cookedPath)))
    {
        m_impl->entries.PopBack();
        return ImportResult<u32>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while mounting an asset"));
    }

    // The watch is registered last: a failure after this point would otherwise
    // leave an id that no entry answers for, and Poll would keep waking on it.
    const u32 watchId = m_impl->watcher.Watch(cookedPath);
    if (watchId == 0)
    {
        m_impl->entries.PopBack();
        return ImportResult<u32>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while watching a cooked file"));
    }

    entry->file = std::move(loaded.Value());
    entry->watchId = watchId;
    return ImportResult<u32>::Ok(static_cast<u32>(m_impl->entries.Size() - 1));
}

const CookedAsset* AssetLibrary::Find(const StringView& virtualPath) const noexcept
{
    if (m_impl == nullptr)
    {
        return nullptr;
    }
    for (usize index = 0; index < m_impl->entries.Size(); ++index)
    {
        const MountedAsset& entry = m_impl->entries[index];
        if (AssetIdFromPath(entry.virtualPath.View()) == AssetIdFromPath(virtualPath))
        {
            return &entry.file.Asset();
        }
    }
    return nullptr;
}

const CookedAsset* AssetLibrary::At(usize index) const noexcept
{
    if (m_impl == nullptr || index >= m_impl->entries.Size())
    {
        return nullptr;
    }
    return &m_impl->entries[index].file.Asset();
}

StringView AssetLibrary::PathAt(usize index) const noexcept
{
    if (m_impl == nullptr || index >= m_impl->entries.Size())
    {
        return StringView{};
    }
    return m_impl->entries[index].virtualPath.View();
}

usize AssetLibrary::Count() const noexcept
{
    return m_impl != nullptr ? m_impl->entries.Size() : 0;
}

ImportStatus AssetLibrary::Reload(const StringView& virtualPath) noexcept
{
    if (m_impl == nullptr)
    {
        return Failure(ErrorCode::OutOfRange, "asset library is not initialized");
    }
    MountedAsset* entry = m_impl->FindMutable(virtualPath);
    if (entry == nullptr)
    {
        return Failure(ErrorCode::NotFound, "asset is not mounted");
    }
    const ImportStatus status = m_impl->LoadInto(*entry, entry->cookedPath.CStr());
    if (status.IsError())
    {
        return status;
    }
    ++m_impl->generation;
    return ImportStatus::Ok();
}

AssetLibrary::ReloadStats AssetLibrary::Poll() noexcept
{
    ReloadStats stats{};
    if (m_impl == nullptr)
    {
        return stats;
    }

    (void)m_impl->watcher.Poll();
    FileChange change{};
    while (m_impl->watcher.PollChange(change))
    {
        MountedAsset* entry = m_impl->FindByWatch(change.watchId);
        if (entry == nullptr)
        {
            continue;
        }
        if (change.kind == cyber::platform::FileChangeKind::Deleted)
        {
            // The last good payload stays resident; a missing file is reported
            // as a failed reload rather than silently unloading the asset.
            ++stats.failed;
            continue;
        }
        if (m_impl->LoadInto(*entry, entry->cookedPath.CStr()).IsError())
        {
            ++stats.failed;
            continue;
        }
        ++m_impl->generation;
        ++stats.reloaded;
    }
    return stats;
}

u64 AssetLibrary::Generation() const noexcept
{
    return m_impl != nullptr ? m_impl->generation : 0;
}

void AssetLibrary::Clear() noexcept
{
    if (m_impl == nullptr)
    {
        return;
    }
    m_impl->entries.Clear();
    m_impl->watcher.UnwatchAll();
}

}  // namespace cyber::assets
