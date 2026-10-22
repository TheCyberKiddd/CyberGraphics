#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>
#include <cyber/platform/types.h>

namespace cyber::platform
{

// Reads the whole file into an allocator-backed byte array. Missing files return
// ErrorCode::NotFound. The returned array owns its storage.
[[nodiscard]] core::Result<memory::DynamicArray<memory::Byte>, core::Error>
ReadFile(const char* utf8Path, memory::Allocator& allocator) noexcept;

// Writes all bytes, creating or truncating the file.
[[nodiscard]] core::Status<core::Error> WriteFile(const char* utf8Path,
                                                  memory::Span<const memory::Byte> bytes) noexcept;

[[nodiscard]] core::Result<u64, core::Error> FileSize(const char* utf8Path) noexcept;
[[nodiscard]] bool Exists(const char* utf8Path) noexcept;
[[nodiscard]] bool IsDirectory(const char* utf8Path) noexcept;

[[nodiscard]] core::Status<core::Error> CreateDirectories(const char* utf8Path) noexcept;
[[nodiscard]] core::Status<core::Error> DeleteFile(const char* utf8Path) noexcept;

// Absolute paths returned in UTF-8. The caller owns the returned string.
[[nodiscard]] core::Result<memory::String, core::Error>
CurrentDirectory(memory::Allocator& allocator) noexcept;
[[nodiscard]] core::Result<memory::String, core::Error>
ExecutableDirectory(memory::Allocator& allocator) noexcept;

// Read-only memory mapping. The mapping is move-only and owns the file handle,
// the OS mapping, and the view. A zero-length file exposes an empty span.
class MemoryMappedFile
{
public:
    MemoryMappedFile() noexcept = default;
    ~MemoryMappedFile();

    MemoryMappedFile(const MemoryMappedFile&) = delete;
    MemoryMappedFile& operator=(const MemoryMappedFile&) = delete;

    MemoryMappedFile(MemoryMappedFile&& other) noexcept;
    MemoryMappedFile& operator=(MemoryMappedFile&& other) noexcept;

    [[nodiscard]] static core::Result<MemoryMappedFile, core::Error>
    Open(const char* utf8Path) noexcept;

    [[nodiscard]] memory::Span<const memory::Byte> Bytes() const noexcept;
    [[nodiscard]] u64 Size() const noexcept;

private:
    void* m_file = nullptr;
    void* m_mapping = nullptr;
    const memory::Byte* m_data = nullptr;
    u64 m_size = 0;
    i32 m_fd = -1;
};

enum class FileChangeKind : u8
{
    Created = 0,
    Modified,
    Deleted,
};

struct FileChange
{
    u32 watchId = 0;
    FileChangeKind kind = FileChangeKind::Modified;
};

// Polling file watcher. Watch IDs are stable until UnwatchAll. Paths are owned
// by the watcher for the lifetime of the watch.
class FileWatcher
{
public:
    explicit FileWatcher(memory::Allocator& allocator) noexcept;
    ~FileWatcher();

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // Returns 0 on allocation or path failure. A returned ID is valid until
    // UnwatchAll.
    [[nodiscard]] u32 Watch(const char* utf8Path) noexcept;
    void UnwatchAll() noexcept;

    [[nodiscard]] u32 WatchCount() const noexcept;
    [[nodiscard]] memory::StringView Path(u32 watchId) const noexcept;

    // Compares timestamps and queues changes. Returns the number of changes
    // queued by this call.
    [[nodiscard]] u32 Poll() noexcept;

    // Pops one queued change. Returns false when no change is pending.
    [[nodiscard]] bool PollChange(FileChange& change) noexcept;

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}  // namespace cyber::platform
