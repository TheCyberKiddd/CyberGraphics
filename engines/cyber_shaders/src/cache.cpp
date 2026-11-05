#include <cyber/core/hash.h>
#include <cyber/platform/filesystem.h>
#include <cyber/shaders/cache.h>

#include <cstdio>
#include <cstring>
#include <utility>

namespace cyber::shaders
{
namespace
{

using cyber::core::Error;
using cyber::core::ErrorCode;
using cyber::core::Result;
using cyber::core::Status;
using cyber::core::u8;
using cyber::core::usize;
using cyber::memory::Byte;
using cyber::memory::DynamicArray;
using cyber::memory::Span;
using cyber::memory::String;
using cyber::memory::StringView;

constexpr u32 kMagic = 0x43534743U;  // "CGSC" read as little-endian bytes
constexpr usize kHeaderBytes = 56;
constexpr u64 kPayloadSeed = 0x4347534341434845ULL;  // "CGSCACHE"
// Generated HLSL is bounded by the compiler's source limit times a small
// expansion; anything larger is not ours.
constexpr u32 kMaximumHlslBytes = 4U * 1024U * 1024U;

class Writer
{
public:
    explicit Writer(DynamicArray<Byte>& out) noexcept : m_out(out) {}

    void U8(u8 value) noexcept
    {
        m_ok = m_ok && m_out.TryPushBack(static_cast<Byte>(value));
    }

    void U32(u32 value) noexcept
    {
        for (u32 shift = 0; shift < 32; shift += 8)
        {
            U8(static_cast<u8>((value >> shift) & 0xFFU));
        }
    }

    void U64(u64 value) noexcept
    {
        for (u32 shift = 0; shift < 64; shift += 8)
        {
            U8(static_cast<u8>((value >> shift) & 0xFFU));
        }
    }

    void Bytes(const char* data, usize size) noexcept
    {
        for (usize index = 0; index < size; ++index)
        {
            U8(static_cast<u8>(data[index]));
        }
    }

    void Name(const ReflectedName& name) noexcept
    {
        U8(name.size);
        Bytes(name.text.data(), name.size);
    }

    [[nodiscard]] bool Ok() const noexcept
    {
        return m_ok;
    }

private:
    DynamicArray<Byte>& m_out;
    bool m_ok = true;
};

class Reader
{
public:
    explicit Reader(Span<const Byte> bytes) noexcept : m_bytes(bytes) {}

    [[nodiscard]] u8 U8() noexcept
    {
        if (m_offset >= m_bytes.Size())
        {
            m_ok = false;
            return 0;
        }
        return static_cast<u8>(m_bytes[m_offset++]);
    }

    [[nodiscard]] u32 U32() noexcept
    {
        u32 value = 0;
        for (u32 shift = 0; shift < 32; shift += 8)
        {
            value |= static_cast<u32>(U8()) << shift;
        }
        return value;
    }

    [[nodiscard]] u64 U64() noexcept
    {
        u64 value = 0;
        for (u32 shift = 0; shift < 64; shift += 8)
        {
            value |= static_cast<u64>(U8()) << shift;
        }
        return value;
    }

    [[nodiscard]] bool Name(ReflectedName& name) noexcept
    {
        const u8 size = U8();
        if (!m_ok || size > kMaxNameLength || Remaining() < size)
        {
            m_ok = false;
            return false;
        }
        name = ReflectedName{};
        for (u8 index = 0; index < size; ++index)
        {
            const char value = static_cast<char>(U8());
            if (value == '\0')
            {
                m_ok = false;
                return false;
            }
            name.text[index] = value;
        }
        name.size = size;
        return m_ok;
    }

    [[nodiscard]] usize Remaining() const noexcept
    {
        return m_offset <= m_bytes.Size() ? m_bytes.Size() - m_offset : 0;
    }

    [[nodiscard]] const char* Here() const noexcept
    {
        return reinterpret_cast<const char*>(m_bytes.Data() + m_offset);
    }

    void Skip(usize size) noexcept
    {
        if (Remaining() < size)
        {
            m_ok = false;
            return;
        }
        m_offset += size;
    }

    void Fail() noexcept
    {
        m_ok = false;
    }

    [[nodiscard]] bool Ok() const noexcept
    {
        return m_ok;
    }

private:
    Span<const Byte> m_bytes;
    usize m_offset = 0;
    bool m_ok = true;
};

void WriteVariable(Writer& writer, const StageVariable& variable) noexcept
{
    writer.Name(variable.name);
    writer.U8(static_cast<u8>(variable.type));
    writer.U8(static_cast<u8>(variable.builtin));
    writer.U32(variable.location);
}

[[nodiscard]] bool ReadVariable(Reader& reader, StageVariable& variable) noexcept
{
    if (!reader.Name(variable.name))
    {
        return false;
    }
    const u8 type = reader.U8();
    const u8 builtin = reader.U8();
    variable.location = reader.U32();
    if (type > static_cast<u8>(ValueType::Uint3) ||
        builtin > static_cast<u8>(Builtin::DispatchThreadId))
    {
        reader.Fail();
        return false;
    }
    variable.type = static_cast<ValueType>(type);
    variable.builtin = static_cast<Builtin>(builtin);
    return reader.Ok();
}

[[nodiscard]] Result<CompiledShader, Error> ParseError() noexcept
{
    return Result<CompiledShader, Error>::Err(
        Error{ErrorCode::ParseError, "shader cache: malformed payload"});
}

[[nodiscard]] Result<CompiledShader, Error> Miss() noexcept
{
    return Result<CompiledShader, Error>::Err(Error{ErrorCode::NotFound, "shader cache: miss"});
}

[[nodiscard]] u64 HeaderU64(const Byte* bytes, usize offset) noexcept
{
    u64 value = 0;
    for (u32 index = 0; index < 8; ++index)
    {
        value |= static_cast<u64>(static_cast<u8>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

[[nodiscard]] u32 HeaderU32(const Byte* bytes, usize offset) noexcept
{
    u32 value = 0;
    for (u32 index = 0; index < 4; ++index)
    {
        value |= static_cast<u32>(static_cast<u8>(bytes[offset + index])) << (index * 8U);
    }
    return value;
}

[[nodiscard]] u64 FileNameHash(const ShaderCacheKey& key) noexcept
{
    u64 hash = core::HashCombine(key.sourceHash, key.permutationHash);
    hash = core::HashCombine(hash, key.targetHash);
    return core::HashCombine(hash, key.compilerVersion);
}

}  // namespace

ShaderCacheKey MakeShaderCacheKey(StringView source, const PermutationKey& permutation,
                                  StringView target) noexcept
{
    ShaderCacheKey key{};
    key.sourceHash = core::HashBytes(source.Data(), source.Size());
    key.permutationHash = permutation.hash;
    key.targetHash = core::HashBytes(target.Data(), target.Size());
    key.compilerVersion = kCompilerVersion;
    return key;
}

bool SerializeCompiledShader(const CompiledShader& shader, DynamicArray<Byte>& out) noexcept
{
    const Reflection& reflection = shader.reflection;
    if (shader.hlsl.Size() > kMaximumHlslBytes || reflection.inputCount > kMaxStageVariables ||
        reflection.outputCount > kMaxStageVariables || reflection.resourceCount > kMaxResources ||
        reflection.memberCount > kMaxConstantMembers || reflection.keywordCount > kMaxKeywords)
    {
        return false;
    }
    Writer writer(out);
    writer.U8(static_cast<u8>(shader.stage));
    writer.U32(shader.permutation.mask);
    writer.U64(shader.permutation.hash);
    writer.U32(static_cast<u32>(shader.hlsl.Size()));
    writer.Bytes(shader.hlsl.CStr(), shader.hlsl.Size());

    for (u32 axis = 0; axis < 3; ++axis)
    {
        writer.U32(reflection.threadGroupSize[axis]);
    }
    writer.U32(reflection.inputCount);
    writer.U32(reflection.outputCount);
    writer.U32(reflection.resourceCount);
    writer.U32(reflection.memberCount);
    writer.U32(reflection.keywordCount);
    for (u32 index = 0; index < reflection.inputCount; ++index)
    {
        WriteVariable(writer, reflection.inputs[index]);
    }
    for (u32 index = 0; index < reflection.outputCount; ++index)
    {
        WriteVariable(writer, reflection.outputs[index]);
    }
    for (u32 index = 0; index < reflection.resourceCount; ++index)
    {
        const ResourceBinding& binding = reflection.resources[index];
        writer.Name(binding.name);
        writer.U8(static_cast<u8>(binding.kind));
        writer.U32(binding.registerIndex);
        writer.U32(binding.registerSpace);
        writer.U32(binding.sizeBytes);
        writer.U32(binding.firstMember);
        writer.U32(binding.memberCount);
    }
    for (u32 index = 0; index < reflection.memberCount; ++index)
    {
        const ConstantMember& member = reflection.members[index];
        writer.Name(member.name);
        writer.U8(static_cast<u8>(member.type));
        writer.U32(member.offsetBytes);
        writer.U32(member.sizeBytes);
    }
    for (u32 index = 0; index < reflection.keywordCount; ++index)
    {
        writer.Name(reflection.keywords[index]);
    }
    return writer.Ok();
}

Result<CompiledShader, Error> DeserializeCompiledShader(Span<const Byte> payload,
                                                        memory::Allocator& allocator) noexcept
{
    Reader reader(payload);
    const u8 stage = reader.U8();
    PermutationKey permutation{};
    permutation.mask = reader.U32();
    permutation.hash = reader.U64();
    const u32 hlslSize = reader.U32();
    if (!reader.Ok() || stage > static_cast<u8>(Stage::Compute) || hlslSize == 0 ||
        hlslSize > kMaximumHlslBytes || reader.Remaining() < hlslSize ||
        permutation.mask >= (1U << kMaxKeywords))
    {
        return ParseError();
    }
    for (u32 index = 0; index < hlslSize; ++index)
    {
        if (reader.Here()[index] == '\0')
        {
            return ParseError();
        }
    }

    CompiledShader shader{static_cast<Stage>(stage), String(allocator), Reflection{}, permutation};
    if (!shader.hlsl.TryAppend(StringView(reader.Here(), hlslSize)))
    {
        return Result<CompiledShader, Error>::Err(
            Error{ErrorCode::Internal, "shader cache: HLSL allocation failed"});
    }
    reader.Skip(hlslSize);

    Reflection& reflection = shader.reflection;
    reflection.stage = shader.stage;
    for (u32 axis = 0; axis < 3; ++axis)
    {
        reflection.threadGroupSize[axis] = reader.U32();
    }
    reflection.inputCount = reader.U32();
    reflection.outputCount = reader.U32();
    reflection.resourceCount = reader.U32();
    reflection.memberCount = reader.U32();
    reflection.keywordCount = reader.U32();
    if (!reader.Ok() || reflection.inputCount > kMaxStageVariables ||
        reflection.outputCount > kMaxStageVariables || reflection.resourceCount > kMaxResources ||
        reflection.memberCount > kMaxConstantMembers || reflection.keywordCount > kMaxKeywords)
    {
        return ParseError();
    }
    for (u32 index = 0; index < reflection.inputCount; ++index)
    {
        if (!ReadVariable(reader, reflection.inputs[index]))
        {
            return ParseError();
        }
    }
    for (u32 index = 0; index < reflection.outputCount; ++index)
    {
        if (!ReadVariable(reader, reflection.outputs[index]))
        {
            return ParseError();
        }
    }
    for (u32 index = 0; index < reflection.resourceCount; ++index)
    {
        ResourceBinding& binding = reflection.resources[index];
        if (!reader.Name(binding.name))
        {
            return ParseError();
        }
        const u8 kind = reader.U8();
        binding.registerIndex = reader.U32();
        binding.registerSpace = reader.U32();
        binding.sizeBytes = reader.U32();
        binding.firstMember = reader.U32();
        binding.memberCount = reader.U32();
        const u64 memberEnd = static_cast<u64>(binding.firstMember) + binding.memberCount;
        if (!reader.Ok() || kind > static_cast<u8>(ResourceKind::StorageTexture) ||
            memberEnd > reflection.memberCount)
        {
            return ParseError();
        }
        binding.kind = static_cast<ResourceKind>(kind);
    }
    for (u32 index = 0; index < reflection.memberCount; ++index)
    {
        ConstantMember& member = reflection.members[index];
        if (!reader.Name(member.name))
        {
            return ParseError();
        }
        const u8 type = reader.U8();
        member.offsetBytes = reader.U32();
        member.sizeBytes = reader.U32();
        if (!reader.Ok() || type > static_cast<u8>(ValueType::Uint3))
        {
            return ParseError();
        }
        member.type = static_cast<ValueType>(type);
    }
    for (u32 index = 0; index < reflection.keywordCount; ++index)
    {
        if (!reader.Name(reflection.keywords[index]))
        {
            return ParseError();
        }
    }
    if (!reader.Ok() || reader.Remaining() != 0)
    {
        return ParseError();
    }
    return Result<CompiledShader, Error>::Ok(std::move(shader));
}

ShaderCache::ShaderCache(ShaderCache&& other) noexcept
    : m_path(other.m_path), m_pathSize(other.m_pathSize), m_hits(other.m_hits.load()),
      m_misses(other.m_misses.load()), m_rejected(other.m_rejected.load()),
      m_stores(other.m_stores.load()), m_storeFailures(other.m_storeFailures.load())
{
    other.m_pathSize = 0;
}

ShaderCache& ShaderCache::operator=(ShaderCache&& other) noexcept
{
    if (this != &other)
    {
        m_path = other.m_path;
        m_pathSize = other.m_pathSize;
        m_hits.store(other.m_hits.load());
        m_misses.store(other.m_misses.load());
        m_rejected.store(other.m_rejected.load());
        m_stores.store(other.m_stores.load());
        m_storeFailures.store(other.m_storeFailures.load());
        other.m_pathSize = 0;
    }
    return *this;
}

Result<ShaderCache, Error> ShaderCache::Open(StringView directory) noexcept
{
    if (directory.Empty() || directory.Size() > kMaxShaderCachePath)
    {
        return Result<ShaderCache, Error>::Err(
            Error{ErrorCode::InvalidArgument, "shader cache: directory path is empty or too long"});
    }
    for (usize index = 0; index < directory.Size(); ++index)
    {
        if (directory[index] == '\0')
        {
            return Result<ShaderCache, Error>::Err(
                Error{ErrorCode::InvalidArgument, "shader cache: directory path contains NUL"});
        }
    }
    ShaderCache cache;
    std::memcpy(cache.m_path.data(), directory.Data(), directory.Size());
    cache.m_path[directory.Size()] = '\0';
    cache.m_pathSize = static_cast<u32>(directory.Size());
    if (!platform::IsDirectory(cache.m_path.data()))
    {
        const Status<Error> created = platform::CreateDirectories(cache.m_path.data());
        if (!created)
        {
            return Result<ShaderCache, Error>::Err(created.Error());
        }
    }
    return Result<ShaderCache, Error>::Ok(std::move(cache));
}

bool ShaderCache::EntryPath(const ShaderCacheKey& key, char* buffer, u32 bufferSize) const noexcept
{
    if (!IsOpen() || buffer == nullptr)
    {
        return false;
    }
    const int written = std::snprintf(buffer, bufferSize, "%s/%016llx.cgsc", m_path.data(),
                                      static_cast<unsigned long long>(FileNameHash(key)));
    return written > 0 && static_cast<u32>(written) < bufferSize;
}

Result<CompiledShader, Error> ShaderCache::Load(const ShaderCacheKey& key,
                                                memory::Allocator& allocator) noexcept
{
    char path[kMaxShaderCachePath + 32]{};
    if (!EntryPath(key, path, sizeof(path)))
    {
        m_misses.fetch_add(1, std::memory_order_relaxed);
        return Miss();
    }
    auto file = platform::ReadFile(path, allocator);
    if (!file)
    {
        m_misses.fetch_add(1, std::memory_order_relaxed);
        return Miss();
    }
    const DynamicArray<Byte>& bytes = file.Value();
    const auto reject = [this]() noexcept
    {
        m_rejected.fetch_add(1, std::memory_order_relaxed);
        m_misses.fetch_add(1, std::memory_order_relaxed);
        return Miss();
    };
    if (bytes.Size() < kHeaderBytes)
    {
        return reject();
    }
    const Byte* data = bytes.Data();
    ShaderCacheKey stored{};
    const u32 magic = HeaderU32(data, 0);
    const u32 format = HeaderU32(data, 4);
    stored.compilerVersion = HeaderU32(data, 8);
    const u32 reserved = HeaderU32(data, 12);
    stored.sourceHash = HeaderU64(data, 16);
    stored.permutationHash = HeaderU64(data, 24);
    stored.targetHash = HeaderU64(data, 32);
    const u64 payloadSize = HeaderU64(data, 40);
    const u64 checksum = HeaderU64(data, 48);
    if (magic != kMagic || format != kShaderCacheFormatVersion || reserved != 0 ||
        !(stored == key) || payloadSize != bytes.Size() - kHeaderBytes)
    {
        return reject();
    }
    const Span<const Byte> payload(data + kHeaderBytes, static_cast<usize>(payloadSize));
    if (core::HashBytes(payload.Data(), payload.Size(), kPayloadSeed) != checksum)
    {
        return reject();
    }
    auto shader = DeserializeCompiledShader(payload, allocator);
    if (!shader)
    {
        return reject();
    }
    if (shader.Value().permutation.hash != key.permutationHash)
    {
        return reject();
    }
    m_hits.fetch_add(1, std::memory_order_relaxed);
    return shader;
}

Status<Error> ShaderCache::Store(const ShaderCacheKey& key, const CompiledShader& shader,
                                 memory::Allocator& allocator) noexcept
{
    const auto failed = [this](Error error) noexcept
    {
        m_storeFailures.fetch_add(1, std::memory_order_relaxed);
        return Status<Error>::Err(error);
    };
    char path[kMaxShaderCachePath + 32]{};
    if (!EntryPath(key, path, sizeof(path)))
    {
        return failed(Error{ErrorCode::InvalidArgument, "shader cache: cache is not open"});
    }
    DynamicArray<Byte> bytes(allocator);
    if (!bytes.TryResize(kHeaderBytes) || !SerializeCompiledShader(shader, bytes))
    {
        return failed(Error{ErrorCode::Internal, "shader cache: serialization failed"});
    }
    const usize payloadSize = bytes.Size() - kHeaderBytes;
    const u64 checksum = core::HashBytes(bytes.Data() + kHeaderBytes, payloadSize, kPayloadSeed);

    DynamicArray<Byte> header(allocator);
    Writer writer(header);
    writer.U32(kMagic);
    writer.U32(kShaderCacheFormatVersion);
    writer.U32(key.compilerVersion);
    writer.U32(0);
    writer.U64(key.sourceHash);
    writer.U64(key.permutationHash);
    writer.U64(key.targetHash);
    writer.U64(static_cast<u64>(payloadSize));
    writer.U64(checksum);
    if (!writer.Ok() || header.Size() != kHeaderBytes)
    {
        return failed(Error{ErrorCode::Internal, "shader cache: header allocation failed"});
    }
    std::memcpy(bytes.Data(), header.Data(), kHeaderBytes);

    const Status<Error> written =
        platform::WriteFile(path, Span<const Byte>(bytes.Data(), bytes.Size()));
    if (!written)
    {
        return failed(written.Error());
    }
    m_stores.fetch_add(1, std::memory_order_relaxed);
    return Status<Error>::Ok();
}

ShaderCacheStats ShaderCache::Stats() const noexcept
{
    ShaderCacheStats stats{};
    stats.hits = m_hits.load(std::memory_order_relaxed);
    stats.misses = m_misses.load(std::memory_order_relaxed);
    stats.rejected = m_rejected.load(std::memory_order_relaxed);
    stats.stores = m_stores.load(std::memory_order_relaxed);
    stats.storeFailures = m_storeFailures.load(std::memory_order_relaxed);
    return stats;
}

}  // namespace cyber::shaders
