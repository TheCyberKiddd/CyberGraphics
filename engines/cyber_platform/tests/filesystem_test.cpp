#include <cyber/memory/heap_allocator.h>
#include <cyber/platform/filesystem.h>
#include <cyber/test/test.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>

using namespace cyber::platform;
using namespace cyber::memory;

namespace
{

std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path ScratchDirectory()
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cyber_platform_test";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return root;
}

}  // namespace

CG_TEST(platform_filesystem_read_write)
{
    HeapAllocator heap;
    const std::filesystem::path root = ScratchDirectory();
    const std::filesystem::path nested = root / "nested" / "deeper";
    const std::string nestedText = ToUtf8(nested);
    CG_EXPECT_TRUE(static_cast<bool>(CreateDirectories(nestedText.c_str())));
    CG_EXPECT_TRUE(IsDirectory(nestedText.c_str()));

    const std::filesystem::path file = root / "read_write.bin";
    const std::string fileText = ToUtf8(file);

    const Byte bytes[] = {Byte{1}, Byte{2}, Byte{3}, Byte{4}, Byte{5}, Byte{6}};
    CG_EXPECT_TRUE(static_cast<bool>(WriteFile(fileText.c_str(), Span<const Byte>(bytes))));

    auto read = ReadFile(fileText.c_str(), heap);
    CG_REQUIRE(static_cast<bool>(read));
    CG_EXPECT_EQ(read.Value().Size(), usize{6});
    CG_EXPECT_EQ(std::memcmp(read.Value().Data(), bytes, sizeof(bytes)), 0);

    auto missing = ReadFile(ToUtf8(root / "missing.bin").c_str(), heap);
    CG_EXPECT_FALSE(static_cast<bool>(missing));
    if (!missing)
    {
        CG_EXPECT_TRUE(missing.Error().code == cyber::core::ErrorCode::NotFound);
    }

    auto missingSize = FileSize(ToUtf8(root / "missing.bin").c_str());
    CG_EXPECT_FALSE(static_cast<bool>(missingSize));

    CG_EXPECT_FALSE(Exists(ToUtf8(root / "missing.bin").c_str()));
    CG_EXPECT_FALSE(IsDirectory(fileText.c_str()));

    auto removed = DeleteFile(fileText.c_str());
    CG_EXPECT_TRUE(static_cast<bool>(removed));
    CG_EXPECT_FALSE(Exists(fileText.c_str()));
    CG_EXPECT_FALSE(static_cast<bool>(DeleteFile(fileText.c_str())));
}

CG_TEST(platform_filesystem_paths)
{
    HeapAllocator heap;
    auto current = CurrentDirectory(heap);
    CG_REQUIRE(static_cast<bool>(current));
    CG_EXPECT_TRUE(current.Value().Size() > 0);

    auto executable = ExecutableDirectory(heap);
    CG_REQUIRE(static_cast<bool>(executable));
    CG_EXPECT_TRUE(executable.Value().Size() > 0);
}

CG_TEST(platform_memory_mapped_file)
{
    const std::filesystem::path root = ScratchDirectory();
    const std::filesystem::path file = root / "mapped.bin";
    const std::string fileText = ToUtf8(file);

    const Byte bytes[] = {Byte{9}, Byte{8}, Byte{7}, Byte{6}};
    CG_REQUIRE(static_cast<bool>(WriteFile(fileText.c_str(), Span<const Byte>(bytes))));

    auto mapped = MemoryMappedFile::Open(fileText.c_str());
    CG_REQUIRE(static_cast<bool>(mapped));
    CG_EXPECT_EQ(mapped.Value().Size(), u64{4});
    CG_EXPECT_EQ(mapped.Value().Bytes().Size(), usize{4});
    CG_EXPECT_EQ(std::memcmp(mapped.Value().Bytes().Data(), bytes, sizeof(bytes)), 0);

    MemoryMappedFile moved = std::move(mapped.Value());
    CG_EXPECT_EQ(moved.Size(), u64{4});
    CG_EXPECT_EQ(std::memcmp(moved.Bytes().Data(), bytes, sizeof(bytes)), 0);

    const std::filesystem::path emptyFile = root / "empty.bin";
    const std::string emptyText = ToUtf8(emptyFile);
    CG_REQUIRE(static_cast<bool>(WriteFile(emptyText.c_str(), Span<const Byte>())));
    auto empty = MemoryMappedFile::Open(emptyText.c_str());
    CG_REQUIRE(static_cast<bool>(empty));
    CG_EXPECT_EQ(empty.Value().Size(), u64{0});
    CG_EXPECT_EQ(empty.Value().Bytes().Size(), usize{0});

    auto missing = MemoryMappedFile::Open(ToUtf8(root / "missing.bin").c_str());
    CG_EXPECT_FALSE(static_cast<bool>(missing));
}

CG_TEST(platform_file_watcher)
{
    const std::filesystem::path root = ScratchDirectory();
    const std::filesystem::path file = root / "watched.txt";
    const std::string fileText = ToUtf8(file);

    std::error_code error;
    std::filesystem::remove(file, error);

    HeapAllocator heap;
    FileWatcher watcher(heap);
    const u32 watch = watcher.Watch(fileText.c_str());
    CG_EXPECT_EQ(watch, u32{1});
    CG_EXPECT_EQ(watcher.WatchCount(), u32{1});
    CG_EXPECT_EQ(watcher.Path(watch).Size(), fileText.size());
    CG_EXPECT_EQ(watcher.Poll(), u32{0});

    const Byte bytes[] = {Byte{'h'}, Byte{'i'}};
    CG_REQUIRE(static_cast<bool>(WriteFile(fileText.c_str(), Span<const Byte>(bytes))));
    CG_EXPECT_EQ(watcher.Poll(), u32{1});
    FileChange change{};
    CG_EXPECT_TRUE(watcher.PollChange(change));
    CG_EXPECT_EQ(change.watchId, watch);
    CG_EXPECT_TRUE(change.kind == FileChangeKind::Created);

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const Byte changed[] = {Byte{'b'}, Byte{'y'}, Byte{'e'}};
    CG_REQUIRE(static_cast<bool>(WriteFile(fileText.c_str(), Span<const Byte>(changed))));
    const u32 modified = watcher.Poll();
    CG_EXPECT_TRUE(modified <= 1);
    if (modified == 1)
    {
        CG_EXPECT_TRUE(watcher.PollChange(change));
        CG_EXPECT_TRUE(change.kind == FileChangeKind::Modified);
    }

    CG_REQUIRE(static_cast<bool>(DeleteFile(fileText.c_str())));
    CG_EXPECT_EQ(watcher.Poll(), u32{1});
    CG_EXPECT_TRUE(watcher.PollChange(change));
    CG_EXPECT_TRUE(change.kind == FileChangeKind::Deleted);

    watcher.UnwatchAll();
    CG_EXPECT_EQ(watcher.WatchCount(), u32{0});
    CG_EXPECT_EQ(watcher.Path(watch).Size(), usize{0});
}

CG_TEST(platform_filesystem_invalid_arguments)
{
    HeapAllocator heap;
    CG_EXPECT_FALSE(static_cast<bool>(ReadFile(nullptr, heap)));
    CG_EXPECT_FALSE(static_cast<bool>(WriteFile(nullptr, Span<const Byte>())));
    CG_EXPECT_FALSE(Exists(nullptr));
    CG_EXPECT_FALSE(IsDirectory(nullptr));
    CG_EXPECT_FALSE(static_cast<bool>(FileSize(nullptr)));
    CG_EXPECT_FALSE(static_cast<bool>(CreateDirectories(nullptr)));
    CG_EXPECT_FALSE(static_cast<bool>(DeleteFile(nullptr)));
    CG_EXPECT_FALSE(static_cast<bool>(MemoryMappedFile::Open(nullptr)));
}
