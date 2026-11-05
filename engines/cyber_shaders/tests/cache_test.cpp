#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/heap_allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string_view.h>
#include <cyber/platform/filesystem.h>
#include <cyber/shaders/cache.h>
#include <cyber/shaders/compiler.h>
#include <cyber/test/test.h>

#include <cstdio>
#include <filesystem>
#include <string>

using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::Byte;
using cyber::memory::DynamicArray;
using cyber::memory::HeapAllocator;
using cyber::memory::Span;
using cyber::memory::StringView;
using cyber::shaders::CompiledShader;
using cyber::shaders::CompileHlsl;
using cyber::shaders::DeserializeCompiledShader;
using cyber::shaders::kHlslTarget;
using cyber::shaders::MakeShaderCacheKey;
using cyber::shaders::SerializeCompiledShader;
using cyber::shaders::ShaderCache;
using cyber::shaders::ShaderCacheKey;

namespace
{

constexpr char kPixel[] = "shader pixel Cube {\n"
                          "    keyword TINT;\n"
                          "    input float4 position @builtin(position);\n"
                          "    input float2 uv @location(0);\n"
                          "    texture2d albedo @register(t0, space0);\n"
                          "    sampler albedoSampler @register(s0, space0);\n"
                          "    uniform float4 tint @register(b0);\n"
                          "    uniform float strength @register(b0);\n"
                          "    return mul(sample(albedo, albedoSampler, input.uv), tint);\n"
                          "}\n";

// A per-test cache directory under the system temporary directory, removed
// before and after the test.
class TempDirectory
{
public:
    explicit TempDirectory(const char* name)
        : m_path((std::filesystem::temp_directory_path() / name).generic_string())
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] StringView View() const noexcept
    {
        return StringView(m_path.c_str(), m_path.size());
    }

private:
    std::string m_path;
};

void ExpectSameShader(const CompiledShader& lhs, const CompiledShader& rhs)
{
    CG_EXPECT_EQ(lhs.stage, rhs.stage);
    CG_EXPECT_EQ(lhs.hlsl.View(), rhs.hlsl.View());
    CG_EXPECT_TRUE(lhs.permutation == rhs.permutation);
    const auto& a = lhs.reflection;
    const auto& b = rhs.reflection;
    CG_EXPECT_EQ(a.inputCount, b.inputCount);
    CG_EXPECT_EQ(a.outputCount, b.outputCount);
    CG_EXPECT_EQ(a.resourceCount, b.resourceCount);
    CG_EXPECT_EQ(a.memberCount, b.memberCount);
    CG_EXPECT_EQ(a.keywordCount, b.keywordCount);
    for (u32 index = 0; index < a.resourceCount; ++index)
    {
        CG_EXPECT_EQ(a.resources[index].name.View(), b.resources[index].name.View());
        CG_EXPECT_EQ(a.resources[index].kind, b.resources[index].kind);
        CG_EXPECT_EQ(a.resources[index].registerIndex, b.resources[index].registerIndex);
        CG_EXPECT_EQ(a.resources[index].sizeBytes, b.resources[index].sizeBytes);
    }
    for (u32 index = 0; index < a.memberCount; ++index)
    {
        CG_EXPECT_EQ(a.members[index].name.View(), b.members[index].name.View());
        CG_EXPECT_EQ(a.members[index].offsetBytes, b.members[index].offsetBytes);
    }
    for (u32 index = 0; index < a.inputCount; ++index)
    {
        CG_EXPECT_EQ(a.inputs[index].name.View(), b.inputs[index].name.View());
        CG_EXPECT_EQ(a.inputs[index].builtin, b.inputs[index].builtin);
    }
    for (u32 index = 0; index < a.keywordCount; ++index)
    {
        CG_EXPECT_EQ(a.keywords[index].View(), b.keywords[index].View());
    }
}

}  // namespace

CG_TEST(shaders_cache_payload_round_trips)
{
    HeapAllocator allocator;
    {
        auto compiled = CompileHlsl("cube_pixel.cgs", kPixel, allocator);
        CG_REQUIRE(static_cast<bool>(compiled));
        DynamicArray<Byte> payload(allocator);
        CG_REQUIRE(SerializeCompiledShader(compiled.Value(), payload));
        auto parsed =
            DeserializeCompiledShader(Span<const Byte>(payload.Data(), payload.Size()), allocator);
        CG_REQUIRE(static_cast<bool>(parsed));
        ExpectSameShader(compiled.Value(), parsed.Value());

        // Every strict prefix is malformed, and so is one trailing byte.
        for (usize size = 0; size < payload.Size(); ++size)
        {
            auto truncated =
                DeserializeCompiledShader(Span<const Byte>(payload.Data(), size), allocator);
            CG_EXPECT_FALSE(static_cast<bool>(truncated));
        }
        CG_REQUIRE(payload.TryPushBack(Byte{0}));
        CG_EXPECT_FALSE(static_cast<bool>(DeserializeCompiledShader(
            Span<const Byte>(payload.Data(), payload.Size()), allocator)));
    }
    CG_EXPECT_EQ(allocator.Stats().used, usize{0});
}

CG_TEST(shaders_cache_payload_survives_corruption_sweep)
{
    HeapAllocator allocator;
    auto compiled = CompileHlsl("cube_pixel.cgs", kPixel, allocator);
    CG_REQUIRE(static_cast<bool>(compiled));
    DynamicArray<Byte> payload(allocator);
    CG_REQUIRE(SerializeCompiledShader(compiled.Value(), payload));
    u32 random = 0x5EEDU;
    for (u32 round = 0; round < 2048; ++round)
    {
        DynamicArray<Byte> mutated(allocator);
        CG_REQUIRE(mutated.TryResize(payload.Size()));
        for (usize index = 0; index < payload.Size(); ++index)
        {
            mutated[index] = payload[index];
        }
        for (u32 flip = 0; flip < 1 + round % 4; ++flip)
        {
            random = random * 1664525U + 1013904223U;
            const usize at = random % payload.Size();
            mutated[at] = static_cast<Byte>(static_cast<u32>(mutated[at]) ^ (random >> 24 | 1U));
        }
        auto parsed =
            DeserializeCompiledShader(Span<const Byte>(mutated.Data(), mutated.Size()), allocator);
        if (parsed)
        {
            // A mutation the format cannot detect (inside a name or HLSL text)
            // must still yield bounded, consistent reflection.
            CG_EXPECT_TRUE(parsed.Value().reflection.resourceCount <=
                           cyber::shaders::kMaxResources);
        }
    }
}

CG_TEST(shaders_cache_store_load_and_reject)
{
    TempDirectory directory("cyber_shaders_cache_test");
    HeapAllocator allocator;
    auto opened = ShaderCache::Open(directory.View());
    CG_REQUIRE(static_cast<bool>(opened));
    ShaderCache cache = std::move(opened.Value());
    CG_EXPECT_TRUE(cache.IsOpen());

    auto compiled = CompileHlsl("cube_pixel.cgs", kPixel, allocator);
    CG_REQUIRE(static_cast<bool>(compiled));
    const ShaderCacheKey key =
        MakeShaderCacheKey(kPixel, compiled.Value().permutation, kHlslTarget);

    auto missing = cache.Load(key, allocator);
    CG_REQUIRE(!missing);
    CG_EXPECT_EQ(missing.Error().code, ErrorCode::NotFound);

    CG_REQUIRE(cache.Store(key, compiled.Value(), allocator).IsOk());
    {
        auto loaded = cache.Load(key, allocator);
        CG_REQUIRE(static_cast<bool>(loaded));
        ExpectSameShader(compiled.Value(), loaded.Value());
    }

    // Any field of the key changing is a miss.
    ShaderCacheKey otherVersion = key;
    ++otherVersion.compilerVersion;
    ShaderCacheKey otherSource = key;
    otherSource.sourceHash ^= 1;
    ShaderCacheKey otherTarget = MakeShaderCacheKey(kPixel, compiled.Value().permutation, "msl");
    CG_EXPECT_FALSE(static_cast<bool>(cache.Load(otherVersion, allocator)));
    CG_EXPECT_FALSE(static_cast<bool>(cache.Load(otherSource, allocator)));
    CG_EXPECT_FALSE(static_cast<bool>(cache.Load(otherTarget, allocator)));

    // A file whose name matches but whose stored key differs (a file-name hash
    // collision, simulated by copying the entry) is rejected, not served.
    char path[600]{};
    char otherPath[600]{};
    CG_REQUIRE(cache.EntryPath(key, path, sizeof(path)));
    CG_REQUIRE(cache.EntryPath(otherVersion, otherPath, sizeof(otherPath)));
    {
        auto bytes = cyber::platform::ReadFile(path, allocator);
        CG_REQUIRE(static_cast<bool>(bytes));
        CG_REQUIRE(cyber::platform::WriteFile(
                       otherPath, Span<const Byte>(bytes.Value().Data(), bytes.Value().Size()))
                       .IsOk());
        CG_EXPECT_FALSE(static_cast<bool>(cache.Load(otherVersion, allocator)));

        // A flipped payload byte fails the checksum.
        DynamicArray<Byte>& corrupt = bytes.Value();
        corrupt[corrupt.Size() - 1] =
            static_cast<Byte>(static_cast<u32>(corrupt[corrupt.Size() - 1]) ^ 0x40U);
        CG_REQUIRE(
            cyber::platform::WriteFile(path, Span<const Byte>(corrupt.Data(), corrupt.Size()))
                .IsOk());
        CG_EXPECT_FALSE(static_cast<bool>(cache.Load(key, allocator)));

        // A truncated file is a miss.
        CG_REQUIRE(cyber::platform::WriteFile(path, Span<const Byte>(corrupt.Data(), 20)).IsOk());
        CG_EXPECT_FALSE(static_cast<bool>(cache.Load(key, allocator)));
    }

    const auto stats = cache.Stats();
    CG_EXPECT_EQ(stats.hits, u64{1});
    CG_EXPECT_EQ(stats.stores, u64{1});
    CG_EXPECT_TRUE(stats.rejected >= 3);
    CG_EXPECT_EQ(stats.misses, u64{7});
}

CG_TEST(shaders_cache_rejects_bad_directories)
{
    CG_EXPECT_FALSE(static_cast<bool>(ShaderCache::Open(StringView{})));
    char tooLong[600]{};
    for (usize index = 0; index + 1 < sizeof(tooLong); ++index)
    {
        tooLong[index] = 'a';
    }
    CG_EXPECT_FALSE(static_cast<bool>(ShaderCache::Open(StringView(tooLong))));

    ShaderCache closed;
    HeapAllocator allocator;
    CG_EXPECT_FALSE(closed.IsOpen());
    CG_EXPECT_FALSE(static_cast<bool>(closed.Load(ShaderCacheKey{}, allocator)));
    auto compiled = CompileHlsl("cube_pixel.cgs", kPixel, allocator);
    CG_REQUIRE(static_cast<bool>(compiled));
    CG_EXPECT_FALSE(closed.Store(ShaderCacheKey{}, compiled.Value(), allocator).IsOk());
}
