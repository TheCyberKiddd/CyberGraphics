#include <cyber/platform/filesystem.h>

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace cyber::platform
{

namespace
{

std::string NativePath(const char* utf8Path)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8Path)))
        .string();
}

}  // namespace

MemoryMappedFile::~MemoryMappedFile()
{
    if (m_mapping != nullptr)
    {
        munmap(m_mapping, static_cast<std::size_t>(m_size));
        m_mapping = nullptr;
        m_data = nullptr;
    }
    if (m_fd >= 0)
    {
        close(m_fd);
        m_fd = -1;
    }
    m_size = 0;
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : m_file(other.m_file), m_mapping(other.m_mapping), m_data(other.m_data), m_size(other.m_size),
      m_fd(other.m_fd)
{
    other.m_file = nullptr;
    other.m_mapping = nullptr;
    other.m_data = nullptr;
    other.m_size = 0;
    other.m_fd = -1;
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept
{
    if (this != &other)
    {
        if (m_mapping != nullptr)
        {
            munmap(m_mapping, static_cast<std::size_t>(m_size));
        }
        if (m_fd >= 0)
        {
            close(m_fd);
        }

        m_file = other.m_file;
        m_mapping = other.m_mapping;
        m_data = other.m_data;
        m_size = other.m_size;
        m_fd = other.m_fd;

        other.m_file = nullptr;
        other.m_mapping = nullptr;
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_fd = -1;
    }
    return *this;
}

core::Result<MemoryMappedFile, core::Error> MemoryMappedFile::Open(const char* utf8Path) noexcept
{
    if (utf8Path == nullptr)
    {
        return core::Result<MemoryMappedFile, core::Error>::Err(
            core::Error{core::ErrorCode::InvalidArgument, "file path is null"});
    }

    const std::string path = NativePath(utf8Path);
    const int descriptor = open(path.c_str(), O_RDONLY);
    if (descriptor < 0)
    {
        const core::ErrorCode code =
            errno == ENOENT ? core::ErrorCode::NotFound : core::ErrorCode::Internal;
        return core::Result<MemoryMappedFile, core::Error>::Err(
            core::Error{code, "failed to open file for memory mapping"});
    }

    struct stat status = {};
    if (fstat(descriptor, &status) != 0 || status.st_size < 0 ||
        static_cast<u64>(status.st_size) > static_cast<u64>(std::numeric_limits<usize>::max()))
    {
        close(descriptor);
        return core::Result<MemoryMappedFile, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to query mapped file size"});
    }

    if (status.st_size == 0)
    {
        MemoryMappedFile result;
        result.m_fd = descriptor;
        result.m_size = 0;
        return core::Result<MemoryMappedFile, core::Error>::Ok(std::move(result));
    }

    void* view = mmap(nullptr, static_cast<std::size_t>(status.st_size), PROT_READ, MAP_PRIVATE,
                      descriptor, 0);
    if (view == MAP_FAILED)
    {
        close(descriptor);
        return core::Result<MemoryMappedFile, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "mmap failed"});
    }

    MemoryMappedFile result;
    result.m_mapping = view;
    result.m_data = static_cast<const memory::Byte*>(view);
    result.m_size = static_cast<u64>(status.st_size);
    result.m_fd = descriptor;
    return core::Result<MemoryMappedFile, core::Error>::Ok(std::move(result));
}

memory::Span<const memory::Byte> MemoryMappedFile::Bytes() const noexcept
{
    return memory::Span<const memory::Byte>(m_data, static_cast<usize>(m_size));
}

u64 MemoryMappedFile::Size() const noexcept
{
    return m_size;
}

}  // namespace cyber::platform
