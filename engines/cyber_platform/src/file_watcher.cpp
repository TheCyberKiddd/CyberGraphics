#include <cyber/platform/filesystem.h>

#include <array>
#include <filesystem>
#include <new>
#include <system_error>
#include <utility>

namespace cyber::platform
{

namespace
{

constexpr usize kMaxPendingChanges = 256;

std::filesystem::path Utf8Path(const char* utf8Path)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8Path)));
}

u64 Timestamp(const std::filesystem::path& path, bool& exists) noexcept
{
    std::error_code error;
    exists = std::filesystem::exists(path, error);
    if (!exists || error)
    {
        return 0;
    }

    const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
    if (error)
    {
        return 0;
    }
    const auto count = time.time_since_epoch().count();
    return count < 0 ? 0 : static_cast<u64>(count);
}

struct WatchedFile
{
    explicit WatchedFile(memory::Allocator& allocator, memory::StringView path)
        : path(allocator, path)
    {
    }

    WatchedFile(const WatchedFile&) = delete;
    WatchedFile& operator=(const WatchedFile&) = delete;
    WatchedFile(WatchedFile&&) noexcept = default;
    WatchedFile& operator=(WatchedFile&&) noexcept = default;

    memory::String path;
    u64 lastWrite = 0;
    bool existed = false;
};

}  // namespace

struct FileWatcher::Impl
{
    explicit Impl(memory::Allocator& allocator) noexcept : allocator(&allocator), files(allocator)
    {
    }

    void PushChange(FileChange change) noexcept
    {
        if (changeCount == kMaxPendingChanges)
        {
            changeHead = (changeHead + 1U) % kMaxPendingChanges;
            --changeCount;
        }
        changes[(changeHead + changeCount) % kMaxPendingChanges] = change;
        ++changeCount;
    }

    memory::Allocator* allocator = nullptr;
    memory::DynamicArray<WatchedFile> files;
    std::array<FileChange, kMaxPendingChanges> changes{};
    usize changeHead = 0;
    usize changeCount = 0;
};

FileWatcher::FileWatcher(memory::Allocator& allocator) noexcept
{
    void* memory = allocator.Allocate(sizeof(Impl), alignof(Impl));
    if (memory == nullptr)
    {
        return;
    }
    m_impl = static_cast<Impl*>(memory);
    std::construct_at(m_impl, allocator);
}

FileWatcher::~FileWatcher()
{
    if (m_impl != nullptr)
    {
        memory::Allocator* allocator = m_impl->allocator;
        m_impl->~Impl();
        if (allocator != nullptr)
        {
            allocator->Deallocate(m_impl);
        }
        m_impl = nullptr;
    }
}

u32 FileWatcher::Watch(const char* utf8Path) noexcept
{
    if (m_impl == nullptr || utf8Path == nullptr || utf8Path[0] == '\0')
    {
        return 0;
    }

    if (m_impl->files.Size() >= static_cast<usize>(kInvalidIndex - 1))
    {
        return 0;
    }

    const std::filesystem::path path = Utf8Path(utf8Path);
    bool exists = false;
    const u64 timestamp = Timestamp(path, exists);

    WatchedFile* entry =
        m_impl->files.TryEmplaceBack(*m_impl->allocator, memory::StringView(utf8Path));
    if (entry == nullptr)
    {
        return 0;
    }
    entry->lastWrite = timestamp;
    entry->existed = exists;
    return static_cast<u32>(m_impl->files.Size());
}

void FileWatcher::UnwatchAll() noexcept
{
    if (m_impl != nullptr)
    {
        m_impl->files.Clear();
        m_impl->changeHead = 0;
        m_impl->changeCount = 0;
    }
}

u32 FileWatcher::WatchCount() const noexcept
{
    return m_impl != nullptr ? static_cast<u32>(m_impl->files.Size()) : 0;
}

memory::StringView FileWatcher::Path(u32 watchId) const noexcept
{
    if (m_impl == nullptr || watchId == 0 || static_cast<usize>(watchId) > m_impl->files.Size())
    {
        return memory::StringView{};
    }
    return m_impl->files[static_cast<usize>(watchId) - 1].path.View();
}

u32 FileWatcher::Poll() noexcept
{
    if (m_impl == nullptr)
    {
        return 0;
    }

    u32 queued = 0;
    for (usize index = 0; index < m_impl->files.Size(); ++index)
    {
        WatchedFile& file = m_impl->files[index];
        const std::filesystem::path path = std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t*>(file.path.CStr()), file.path.Size()));
        bool exists = false;
        const u64 timestamp = Timestamp(path, exists);

        FileChange change{};
        change.watchId = static_cast<u32>(index + 1);

        if (file.existed && !exists)
        {
            change.kind = FileChangeKind::Deleted;
            file.existed = false;
            file.lastWrite = 0;
            m_impl->PushChange(change);
            ++queued;
        }
        else if (!file.existed && exists)
        {
            change.kind = FileChangeKind::Created;
            file.existed = true;
            file.lastWrite = timestamp;
            m_impl->PushChange(change);
            ++queued;
        }
        else if (file.existed && exists && timestamp != file.lastWrite)
        {
            change.kind = FileChangeKind::Modified;
            file.lastWrite = timestamp;
            m_impl->PushChange(change);
            ++queued;
        }
    }
    return queued;
}

bool FileWatcher::PollChange(FileChange& change) noexcept
{
    if (m_impl == nullptr || m_impl->changeCount == 0)
    {
        return false;
    }

    change = m_impl->changes[m_impl->changeHead];
    m_impl->changeHead = (m_impl->changeHead + 1U) % kMaxPendingChanges;
    --m_impl->changeCount;
    return true;
}

}  // namespace cyber::platform
