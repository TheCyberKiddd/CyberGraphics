#include "executable_path.h"

#include <cyber/platform/filesystem.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

namespace cyber::platform
{

namespace
{

core::Error FileError(core::ErrorCode code, const char* message) noexcept
{
    return core::Error{code, message};
}

std::filesystem::path Utf8Path(const char* utf8Path)
{
    if (utf8Path == nullptr || utf8Path[0] == '\0')
    {
        return {};
    }
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8Path)));
}

core::Result<memory::String, core::Error> PathToUtf8String(const std::filesystem::path& path,
                                                           memory::Allocator& allocator) noexcept
{
    const std::u8string utf8 = path.u8string();
    memory::String result(allocator);
    if (!result.TryAssign(
            memory::StringView(reinterpret_cast<const char*>(utf8.data()), utf8.size())))
    {
        return core::Result<memory::String, core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to allocate a path string"));
    }
    return core::Result<memory::String, core::Error>::Ok(std::move(result));
}

}  // namespace

core::Result<memory::DynamicArray<memory::Byte>, core::Error>
ReadFile(const char* utf8Path, memory::Allocator& allocator) noexcept
{
    if (utf8Path == nullptr)
    {
        return core::Result<memory::DynamicArray<memory::Byte>, core::Error>::Err(
            FileError(core::ErrorCode::InvalidArgument, "file path is null"));
    }

    const std::filesystem::path path = Utf8Path(utf8Path);
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error)
    {
        return core::Result<memory::DynamicArray<memory::Byte>, core::Error>::Err(
            FileError(core::ErrorCode::NotFound, "file does not exist"));
    }

    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
    {
        return core::Result<memory::DynamicArray<memory::Byte>, core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to open file for reading"));
    }

    const std::streamoff end = stream.tellg();
    if (end < 0 || static_cast<u64>(end) > static_cast<u64>(std::numeric_limits<usize>::max()))
    {
        return core::Result<memory::DynamicArray<memory::Byte>, core::Error>::Err(
            FileError(core::ErrorCode::Overflow, "file is too large"));
    }

    const usize size = static_cast<usize>(end);
    memory::DynamicArray<memory::Byte> data(allocator);
    if (!data.TryResize(size))
    {
        return core::Result<memory::DynamicArray<memory::Byte>, core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to allocate file contents"));
    }

    if (size > 0)
    {
        stream.seekg(0, std::ios::beg);
        stream.read(reinterpret_cast<char*>(data.Data()), static_cast<std::streamsize>(size));
        if (!stream || static_cast<usize>(stream.gcount()) != size)
        {
            return core::Result<memory::DynamicArray<memory::Byte>, core::Error>::Err(
                FileError(core::ErrorCode::Internal, "failed to read the whole file"));
        }
    }

    return core::Result<memory::DynamicArray<memory::Byte>, core::Error>::Ok(std::move(data));
}

core::Status<core::Error> WriteFile(const char* utf8Path,
                                    memory::Span<const memory::Byte> bytes) noexcept
{
    if (utf8Path == nullptr)
    {
        return core::Status<core::Error>::Err(
            FileError(core::ErrorCode::InvalidArgument, "file path is null"));
    }

    std::ofstream stream(Utf8Path(utf8Path), std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return core::Status<core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to open file for writing"));
    }

    if (!bytes.Empty())
    {
        stream.write(reinterpret_cast<const char*>(bytes.Data()),
                     static_cast<std::streamsize>(bytes.Size()));
        if (!stream)
        {
            return core::Status<core::Error>::Err(
                FileError(core::ErrorCode::Internal, "failed to write the whole file"));
        }
    }
    return core::Status<core::Error>::Ok();
}

core::Result<u64, core::Error> FileSize(const char* utf8Path) noexcept
{
    if (utf8Path == nullptr)
    {
        return core::Result<u64, core::Error>::Err(
            FileError(core::ErrorCode::InvalidArgument, "file path is null"));
    }

    const std::filesystem::path path = Utf8Path(utf8Path);
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error)
    {
        return core::Result<u64, core::Error>::Err(
            FileError(core::ErrorCode::NotFound, "file does not exist"));
    }

    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        return core::Result<u64, core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to query file size"));
    }
    return core::Result<u64, core::Error>::Ok(static_cast<u64>(size));
}

bool Exists(const char* utf8Path) noexcept
{
    if (utf8Path == nullptr)
    {
        return false;
    }
    std::error_code error;
    return std::filesystem::exists(Utf8Path(utf8Path), error) && !error;
}

bool IsDirectory(const char* utf8Path) noexcept
{
    if (utf8Path == nullptr)
    {
        return false;
    }
    std::error_code error;
    return std::filesystem::is_directory(Utf8Path(utf8Path), error) && !error;
}

core::Status<core::Error> CreateDirectories(const char* utf8Path) noexcept
{
    if (utf8Path == nullptr)
    {
        return core::Status<core::Error>::Err(
            FileError(core::ErrorCode::InvalidArgument, "directory path is null"));
    }

    std::error_code error;
    std::filesystem::create_directories(Utf8Path(utf8Path), error);
    if (error)
    {
        return core::Status<core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to create directories"));
    }
    return core::Status<core::Error>::Ok();
}

core::Status<core::Error> DeleteFile(const char* utf8Path) noexcept
{
    if (utf8Path == nullptr)
    {
        return core::Status<core::Error>::Err(
            FileError(core::ErrorCode::InvalidArgument, "file path is null"));
    }

    std::error_code error;
    const bool removed = std::filesystem::remove(Utf8Path(utf8Path), error);
    if (error)
    {
        return core::Status<core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to remove file"));
    }
    if (!removed)
    {
        return core::Status<core::Error>::Err(
            FileError(core::ErrorCode::NotFound, "file does not exist"));
    }
    return core::Status<core::Error>::Ok();
}

core::Result<memory::String, core::Error> CurrentDirectory(memory::Allocator& allocator) noexcept
{
    std::error_code error;
    const std::filesystem::path path = std::filesystem::current_path(error);
    if (error)
    {
        return core::Result<memory::String, core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to query the current directory"));
    }
    return PathToUtf8String(path, allocator);
}

core::Result<memory::String, core::Error> ExecutableDirectory(memory::Allocator& allocator) noexcept
{
    std::filesystem::path path;
    if (!detail::ExecutablePath(path))
    {
        return core::Result<memory::String, core::Error>::Err(
            FileError(core::ErrorCode::Internal, "failed to query the executable path"));
    }
    return PathToUtf8String(path.parent_path(), allocator);
}

}  // namespace cyber::platform
