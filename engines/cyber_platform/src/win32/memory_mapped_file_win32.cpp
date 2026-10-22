#include <cyber/platform/filesystem.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <windows.h>

namespace cyber::platform
{

namespace
{

std::wstring WidePath(const char* utf8Path)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8Path)))
        .wstring();
}

}  // namespace

MemoryMappedFile::~MemoryMappedFile()
{
    if (m_data != nullptr)
    {
        UnmapViewOfFile(m_data);
        m_data = nullptr;
    }
    if (m_mapping != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(m_mapping));
        m_mapping = nullptr;
    }
    if (m_file != nullptr)
    {
        CloseHandle(static_cast<HANDLE>(m_file));
        m_file = nullptr;
    }
    m_size = 0;
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : m_file(other.m_file), m_mapping(other.m_mapping), m_data(other.m_data), m_size(other.m_size)
{
    other.m_file = nullptr;
    other.m_mapping = nullptr;
    other.m_data = nullptr;
    other.m_size = 0;
}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept
{
    if (this != &other)
    {
        if (m_data != nullptr)
        {
            UnmapViewOfFile(m_data);
        }
        if (m_mapping != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(m_mapping));
        }
        if (m_file != nullptr)
        {
            CloseHandle(static_cast<HANDLE>(m_file));
        }

        m_file = other.m_file;
        m_mapping = other.m_mapping;
        m_data = other.m_data;
        m_size = other.m_size;

        other.m_file = nullptr;
        other.m_mapping = nullptr;
        other.m_data = nullptr;
        other.m_size = 0;
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

    const std::wstring path = WidePath(utf8Path);
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const DWORD error = GetLastError();
        const core::ErrorCode code = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                                         ? core::ErrorCode::NotFound
                                         : core::ErrorCode::Internal;
        return core::Result<MemoryMappedFile, core::Error>::Err(
            core::Error{code, "failed to open file for memory mapping"});
    }

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == 0 || size.QuadPart < 0 ||
        static_cast<u64>(size.QuadPart) > static_cast<u64>(std::numeric_limits<usize>::max()))
    {
        CloseHandle(file);
        return core::Result<MemoryMappedFile, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "failed to query mapped file size"});
    }

    if (size.QuadPart == 0)
    {
        MemoryMappedFile result;
        result.m_file = file;
        result.m_size = 0;
        return core::Result<MemoryMappedFile, core::Error>::Ok(std::move(result));
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr)
    {
        CloseHandle(file);
        return core::Result<MemoryMappedFile, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "CreateFileMappingW failed"});
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr)
    {
        CloseHandle(mapping);
        CloseHandle(file);
        return core::Result<MemoryMappedFile, core::Error>::Err(
            core::Error{core::ErrorCode::Internal, "MapViewOfFile failed"});
    }

    MemoryMappedFile result;
    result.m_file = file;
    result.m_mapping = mapping;
    result.m_data = static_cast<const memory::Byte*>(view);
    result.m_size = static_cast<u64>(size.QuadPart);
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
