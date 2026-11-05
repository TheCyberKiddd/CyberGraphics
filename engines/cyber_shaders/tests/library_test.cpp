#include <cyber/jobs/job_system.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>
#include <cyber/platform/filesystem.h>
#include <cyber/shaders/cache.h>
#include <cyber/shaders/library.h>
#include <cyber/test/test.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::jobs::JobSystem;
using cyber::memory::Byte;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::String;
using cyber::memory::StringView;
using cyber::shaders::kPermutationHashSeed;
using cyber::shaders::ShaderCache;
using cyber::shaders::ShaderEvent;
using cyber::shaders::ShaderEventKind;
using cyber::shaders::ShaderId;
using cyber::shaders::ShaderLibrary;
using cyber::shaders::ShaderLibraryDesc;
using cyber::shaders::Stage;

namespace
{

constexpr char kPixelA[] =
    "shader pixel Cube {\n"
    "    keyword TINT;\n"
    "    keyword DEBUG_UV;\n"
    "    input float4 position @builtin(position);\n"
    "    input float2 uv @location(0);\n"
    "    texture2d albedo @register(t0);\n"
    "    sampler albedoSampler @register(s0);\n"
    "    uniform float4 tint @register(b0);\n"
    "    when DEBUG_UV {\n"
    "        return float4(input.uv, 0.0, 1.0);\n"
    "    } else {\n"
    "        when TINT {\n"
    "            return mul(sample(albedo, albedoSampler, input.uv), tint);\n"
    "        } else {\n"
    "            return sample(albedo, albedoSampler, input.uv);\n"
    "        }\n"
    "    }\n"
    "}\n";

constexpr char kPixelB[] = "shader pixel Cube {\n"
                           "    keyword TINT;\n"
                           "    input float2 uv @location(0);\n"
                           "    return float4(input.uv, 1.0, 1.0);\n"
                           "}\n";

constexpr char kBroken[] = "shader pixel Cube {\n"
                           "    input float2 uv @location(0);\n"
                           "    return float4(input.uv, 1.0 1.0);\n"
                           "}\n";

constexpr char kVertex[] =
    "shader vertex Cube {\n"
    "    input float3 position @location(0);\n"
    "    output float4 clipPosition @builtin(position);\n"
    "    uniform float4x4 modelViewProjection @register(b0);\n"
    "    output.clipPosition = mul(modelViewProjection, float4(input.position, 1.0));\n"
    "}\n";

[[nodiscard]] u64 NowNs()
{
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
}

class TempDirectory
{
public:
    explicit TempDirectory(const char* name)
        : m_path((std::filesystem::temp_directory_path() / name).generic_string())
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
        std::filesystem::create_directories(m_path, error);
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] std::string File(const char* name) const
    {
        return m_path + "/" + name;
    }

    [[nodiscard]] const std::string& Path() const noexcept
    {
        return m_path;
    }

private:
    std::string m_path;
};

[[nodiscard]] bool Write(const std::string& path, const char* text)
{
    const StringView view(text);
    return cyber::platform::WriteFile(
               path.c_str(),
               Span<const Byte>(reinterpret_cast<const Byte*>(view.Data()), view.Size()))
        .IsOk();
}

// Rewrite a file and make sure its last-write time moves even on file systems
// with coarse timestamps.
[[nodiscard]] bool Rewrite(const std::string& path, const char* text)
{
    std::error_code error;
    const auto before = std::filesystem::last_write_time(path, error);
    if (!Write(path, text))
    {
        return false;
    }
    std::filesystem::last_write_time(path, before + std::chrono::seconds(2), error);
    return !error;
}

// Calls Update until `predicate` holds or two seconds pass.
template <typename Predicate>
[[nodiscard]] bool UpdateUntil(ShaderLibrary& library, Predicate predicate)
{
    const u64 deadline = NowNs() + 2000000000ULL;
    while (NowNs() < deadline)
    {
        library.Update();
        if (predicate())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

}  // namespace

CG_TEST(shaders_library_rejects_bad_configuration)
{
    HeapAllocator allocator;
    ShaderLibraryDesc desc{};
    desc.maxPermutationsPerShader = 0;
    CG_EXPECT_FALSE(static_cast<bool>(ShaderLibrary::Create(desc, allocator)));
    desc.maxPermutationsPerShader = 257;
    CG_EXPECT_FALSE(static_cast<bool>(ShaderLibrary::Create(desc, allocator)));
    desc.maxPermutationsPerShader = 4;
    desc.target = "";
    CG_EXPECT_FALSE(static_cast<bool>(ShaderLibrary::Create(desc, allocator)));

    desc.target = cyber::shaders::kHlslTarget;
    auto created = ShaderLibrary::Create(desc, allocator);
    CG_REQUIRE(static_cast<bool>(created));
    ShaderLibrary library = std::move(created.Value());
    CG_EXPECT_FALSE(static_cast<bool>(library.Register(StringView{})));
    auto first = library.Register("a.cgs");
    CG_REQUIRE(static_cast<bool>(first));
    auto duplicate = library.Register("a.cgs");
    CG_REQUIRE(!duplicate);
    CG_EXPECT_EQ(duplicate.Error().code, ErrorCode::AlreadyExists);

    const StringView bad[] = {"9LIVES"};
    CG_EXPECT_FALSE(static_cast<bool>(library.RequestPermutation(first.Value(), bad)));
    CG_EXPECT_FALSE(static_cast<bool>(library.RequestPermutation(99, Span<const StringView>{})));
    const StringView names[] = {"A", "B", "C"};
    CG_EXPECT_TRUE(static_cast<bool>(
        library.RequestPermutation(first.Value(), Span<const StringView>(names, 1))));
    CG_EXPECT_TRUE(static_cast<bool>(
        library.RequestPermutation(first.Value(), Span<const StringView>(names, 2))));
    CG_EXPECT_TRUE(static_cast<bool>(
        library.RequestPermutation(first.Value(), Span<const StringView>(names, 3))));
    auto overflow = library.RequestPermutation(first.Value(), Span<const StringView>(names + 1, 1));
    CG_REQUIRE(!overflow);
    CG_EXPECT_EQ(overflow.Error().code, ErrorCode::Overflow);
    CG_EXPECT_FALSE(library.RequestAllPermutations(42).IsOk());
}

CG_TEST(shaders_library_compiles_all_permutations_on_each_worker_count)
{
    const u32 workerCounts[] = {1, 2, 4, 8};
    for (const u32 workers : workerCounts)
    {
        TempDirectory directory("cyber_shaders_library_workers");
        CG_REQUIRE(Write(directory.File("pixel.cgs"), kPixelA));
        CG_REQUIRE(Write(directory.File("vertex.cgs"), kVertex));

        HeapAllocator allocator;
        {
            JobSystem::Desc jobsDesc{};
            jobsDesc.workerCount = workers;
            auto jobs = JobSystem::Create(jobsDesc, allocator);
            CG_REQUIRE(static_cast<bool>(jobs));

            ShaderLibraryDesc desc{};
            desc.jobs = &jobs.Value();
            desc.watchFiles = false;
            auto created = ShaderLibrary::Create(desc, allocator);
            CG_REQUIRE(static_cast<bool>(created));
            ShaderLibrary library = std::move(created.Value());

            auto pixel = library.Register(StringView(directory.File("pixel.cgs").c_str()));
            auto vertex = library.Register(StringView(directory.File("vertex.cgs").c_str()));
            CG_REQUIRE(pixel && vertex);
            CG_REQUIRE(library.RequestAllPermutations(pixel.Value()).IsOk());
            const StringView tint[] = {"TINT"};
            auto tintHash = library.RequestPermutation(pixel.Value(), tint);
            CG_REQUIRE(static_cast<bool>(tintHash));

            library.Update();
            library.WaitIdle();

            CG_EXPECT_EQ(library.Version(pixel.Value()), u32{1});
            CG_EXPECT_EQ(library.Version(vertex.Value()), u32{1});
            const auto stats = library.Stats(pixel.Value());
            CG_EXPECT_EQ(stats.stage, Stage::Pixel);
            CG_EXPECT_EQ(stats.declaredKeywords, u32{2});
            CG_EXPECT_EQ(stats.possiblePermutations, u32{4});
            CG_EXPECT_EQ(stats.compiledPermutations, u32{4});
            CG_EXPECT_EQ(stats.compilations, u64{4});
            CG_EXPECT_TRUE(library.Find(pixel.Value(), kPermutationHashSeed) != nullptr);
            const auto* tinted = library.Find(pixel.Value(), tintHash.Value());
            CG_REQUIRE(tinted != nullptr);
            CG_EXPECT_EQ(tinted->permutation.mask, u32{1});
            CG_EXPECT_TRUE(library.LastError(pixel.Value()) == nullptr);

            u32 compiledEvents = 0;
            ShaderEvent event{};
            while (library.PollEvent(event))
            {
                compiledEvents += event.kind == ShaderEventKind::Compiled ? 1U : 0U;
            }
            CG_EXPECT_EQ(compiledEvents, u32{2});
        }
        CG_EXPECT_EQ(allocator.Stats().used, usize{0});
    }
}

CG_TEST(shaders_library_reuses_the_disk_cache)
{
    TempDirectory directory("cyber_shaders_library_cache");
    CG_REQUIRE(Write(directory.File("pixel.cgs"), kPixelA));
    HeapAllocator allocator;
    auto opened = ShaderCache::Open(StringView((directory.Path() + "/cache").c_str()));
    CG_REQUIRE(static_cast<bool>(opened));
    ShaderCache cache = std::move(opened.Value());

    for (u32 run = 0; run < 2; ++run)
    {
        ShaderLibraryDesc desc{};
        desc.cache = &cache;
        desc.watchFiles = false;
        auto created = ShaderLibrary::Create(desc, allocator);
        CG_REQUIRE(static_cast<bool>(created));
        ShaderLibrary library = std::move(created.Value());
        auto pixel = library.Register(StringView(directory.File("pixel.cgs").c_str()));
        CG_REQUIRE(static_cast<bool>(pixel));
        CG_REQUIRE(library.RequestAllPermutations(pixel.Value()).IsOk());
        library.Update();
        const auto stats = library.Stats(pixel.Value());
        CG_EXPECT_EQ(stats.compiledPermutations, u32{4});
        if (run == 0)
        {
            CG_EXPECT_EQ(stats.compilations, u64{4});
            CG_EXPECT_EQ(stats.cacheHits, u64{0});
        }
        else
        {
            // A warm start compiles nothing.
            CG_EXPECT_EQ(stats.compilations, u64{0});
            CG_EXPECT_EQ(stats.cacheHits, u64{4});
        }
    }
    CG_EXPECT_EQ(cache.Stats().stores, u64{4});
    CG_EXPECT_EQ(cache.Stats().hits, u64{4});
}

CG_TEST(shaders_library_reloads_changed_files_and_keeps_last_good)
{
    TempDirectory directory("cyber_shaders_library_reload");
    const std::string path = directory.File("pixel.cgs");
    CG_REQUIRE(Write(path, kPixelA));
    HeapAllocator allocator;
    {
        JobSystem::Desc jobsDesc{};
        jobsDesc.workerCount = 2;
        auto jobs = JobSystem::Create(jobsDesc, allocator);
        CG_REQUIRE(static_cast<bool>(jobs));
        ShaderLibraryDesc desc{};
        desc.jobs = &jobs.Value();
        desc.pollIntervalMilliseconds = 10;
        auto created = ShaderLibrary::Create(desc, allocator);
        CG_REQUIRE(static_cast<bool>(created));
        ShaderLibrary library = std::move(created.Value());
        auto pixel = library.Register(StringView(path.c_str()));
        CG_REQUIRE(static_cast<bool>(pixel));
        const ShaderId id = pixel.Value();
        CG_REQUIRE(UpdateUntil(library, [&] { return library.Version(id) == 1; }));
        const auto* first = library.Find(id, kPermutationHashSeed);
        CG_REQUIRE(first != nullptr);
        CG_EXPECT_TRUE(first->hlsl.View().ToStd().find(".Sample(") != std::string_view::npos);

        // A broken edit keeps version 1 and its HLSL, and records the error.
        CG_REQUIRE(Rewrite(path, kBroken));
        CG_REQUIRE(UpdateUntil(library, [&] { return library.LastError(id) != nullptr; }));
        CG_EXPECT_EQ(library.Version(id), u32{1});
        const auto* error = library.LastError(id);
        CG_EXPECT_EQ(error->line, u32{3});
        CG_EXPECT_EQ(error->column, u32{33});
        CG_EXPECT_EQ(error->sourceName, StringView(path.c_str()));
        const auto* kept = library.Find(id, kPermutationHashSeed);
        CG_REQUIRE(kept != nullptr);
        CG_EXPECT_TRUE(kept->hlsl.View().ToStd().find(".Sample(") != std::string_view::npos);

        // A fixed edit publishes version 2 and clears the error.
        CG_REQUIRE(Rewrite(path, kPixelB));
        CG_REQUIRE(UpdateUntil(library, [&] { return library.Version(id) == 2; }));
        CG_EXPECT_TRUE(library.LastError(id) == nullptr);
        const auto* second = library.Find(id, kPermutationHashSeed);
        CG_REQUIRE(second != nullptr);
        CG_EXPECT_FALSE(second->hlsl.View().ToStd().find(".Sample(") != std::string_view::npos);

        u32 failed = 0;
        u32 compiled = 0;
        ShaderEvent event{};
        while (library.PollEvent(event))
        {
            failed += event.kind == ShaderEventKind::Failed ? 1U : 0U;
            compiled += event.kind == ShaderEventKind::Compiled ? 1U : 0U;
            CG_EXPECT_TRUE(event.latencyNanoseconds < 2000000000ULL);
        }
        CG_EXPECT_EQ(failed, u32{1});
        CG_EXPECT_EQ(compiled, u32{2});
        CG_EXPECT_EQ(library.Stats(id).failures, u64{1});
    }
    CG_EXPECT_EQ(allocator.Stats().used, usize{0});
}

CG_TEST(shaders_library_writes_permutation_report)
{
    TempDirectory directory("cyber_shaders_library_report");
    CG_REQUIRE(Write(directory.File("pixel.cgs"), kPixelA));
    CG_REQUIRE(Write(directory.File("vertex.cgs"), kVertex));
    HeapAllocator allocator;
    ShaderLibraryDesc desc{};
    desc.watchFiles = false;
    auto created = ShaderLibrary::Create(desc, allocator);
    CG_REQUIRE(static_cast<bool>(created));
    ShaderLibrary library = std::move(created.Value());
    auto pixel = library.Register(StringView(directory.File("pixel.cgs").c_str()));
    auto vertex = library.Register(StringView(directory.File("vertex.cgs").c_str()));
    auto missing = library.Register(StringView(directory.File("missing.cgs").c_str()));
    CG_REQUIRE(pixel && vertex && missing);
    CG_REQUIRE(library.RequestAllPermutations(pixel.Value()).IsOk());
    library.Update();

    String report(allocator);
    CG_REQUIRE(library.WritePermutationReport(report));
    const std::string text(report.CStr(), report.Size());
    CG_EXPECT_TRUE(text.find("# CyberGraphics shader permutation report") != std::string::npos);
    CG_EXPECT_TRUE(text.find(" | pixel | TINT,DEBUG_UV | 4 | 4 | 4 | 4 | 0 | 0 | 1 | -") !=
                   std::string::npos);
    CG_EXPECT_TRUE(text.find(" | vertex | - | 1 | 1 | 1 | 1 | 0 | 0 | 1 | -") != std::string::npos);
    CG_EXPECT_TRUE(text.find("unable to read shader file") != std::string::npos);
    CG_EXPECT_TRUE(text.find("totals: shaders 3, possible 6, compiled 5") != std::string::npos);
}

CG_TEST(shaders_library_limits_declared_permutations)
{
    TempDirectory directory("cyber_shaders_library_limit");
    CG_REQUIRE(Write(directory.File("pixel.cgs"), kPixelA));
    HeapAllocator allocator;
    ShaderLibraryDesc desc{};
    desc.watchFiles = false;
    desc.maxPermutationsPerShader = 2;
    auto created = ShaderLibrary::Create(desc, allocator);
    CG_REQUIRE(static_cast<bool>(created));
    ShaderLibrary library = std::move(created.Value());
    auto pixel = library.Register(StringView(directory.File("pixel.cgs").c_str()));
    CG_REQUIRE(static_cast<bool>(pixel));
    CG_REQUIRE(library.RequestAllPermutations(pixel.Value()).IsOk());
    library.Update();
    CG_EXPECT_EQ(library.Version(pixel.Value()), u32{0});
    CG_REQUIRE(library.LastError(pixel.Value()) != nullptr);
    CG_EXPECT_EQ(library.LastError(pixel.Value())->kind,
                 cyber::shaders::DiagnosticKind::Unsupported);
}
