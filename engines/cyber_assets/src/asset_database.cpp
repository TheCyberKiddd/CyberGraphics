#include <cyber/assets/asset_database.h>
#include <cyber/assets/json.h>
#include <cyber/platform/filesystem.h>

#include <cstdio>
#include <cstring>
#include <utility>

namespace cyber::assets
{
namespace
{

using cyber::core::Byte;
using cyber::core::ErrorCode;
using cyber::core::u32;
using cyber::core::u64;

constexpr const char* kManifestFormatTag = "cyberassetdb";
constexpr usize kHashHexLength = 16;

[[nodiscard]] char HexDigit(u32 value) noexcept
{
    return static_cast<char>(value < 10 ? '0' + value : 'a' + (value - 10));
}

[[nodiscard]] bool IsHexDigit(char value) noexcept
{
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') ||
           (value >= 'A' && value <= 'F');
}

[[nodiscard]] u32 HexValue(char value) noexcept
{
    if (value >= '0' && value <= '9')
    {
        return static_cast<u32>(value - '0');
    }
    if (value >= 'a' && value <= 'f')
    {
        return static_cast<u32>(value - 'a') + 10u;
    }
    return static_cast<u32>(value - 'A') + 10u;
}

void FormatHash(u64 value, char out[kHashHexLength + 1]) noexcept
{
    for (usize index = 0; index < kHashHexLength; ++index)
    {
        const u32 shift = static_cast<u32>((kHashHexLength - 1 - index) * 4);
        out[index] = HexDigit(static_cast<u32>((value >> shift) & 0xfu));
    }
    out[kHashHexLength] = '\0';
}

/// Requires exactly 16 hex digits, so a truncated or padded hash is rejected
/// instead of being read as a different cache key.
[[nodiscard]] bool ParseHash(StringView text, u64& value) noexcept
{
    if (text.Size() != kHashHexLength)
    {
        return false;
    }
    u64 parsed = 0;
    for (usize index = 0; index < kHashHexLength; ++index)
    {
        if (!IsHexDigit(text[index]))
        {
            return false;
        }
        parsed = (parsed << 4u) | HexValue(text[index]);
    }
    value = parsed;
    return true;
}

[[nodiscard]] bool AppendRaw(DynamicArray<char>& out, const char* text) noexcept
{
    const usize length = std::strlen(text);
    for (usize index = 0; index < length; ++index)
    {
        if (!out.TryPushBack(text[index]))
        {
            return false;
        }
    }
    return true;
}

/// Writes a JSON string literal, escaping the characters the format requires.
[[nodiscard]] bool AppendJsonString(DynamicArray<char>& out, StringView text) noexcept
{
    if (!out.TryPushBack('"'))
    {
        return false;
    }
    for (usize index = 0; index < text.Size(); ++index)
    {
        const char value = text[index];
        switch (value)
        {
        case '"':
        case '\\':
            if (!out.TryPushBack('\\') || !out.TryPushBack(value))
            {
                return false;
            }
            break;
        case '\n':
            if (!out.TryPushBack('\\') || !out.TryPushBack('n'))
            {
                return false;
            }
            break;
        case '\r':
            if (!out.TryPushBack('\\') || !out.TryPushBack('r'))
            {
                return false;
            }
            break;
        case '\t':
            if (!out.TryPushBack('\\') || !out.TryPushBack('t'))
            {
                return false;
            }
            break;
        default:
            if (static_cast<unsigned char>(value) < 0x20u)
            {
                char escape[7] = {};
                const int written =
                    std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(value));
                if (written != 6)
                {
                    return false;
                }
                for (usize digit = 0; digit < 6; ++digit)
                {
                    if (!out.TryPushBack(escape[digit]))
                    {
                        return false;
                    }
                }
            }
            else if (!out.TryPushBack(value))
            {
                return false;
            }
            break;
        }
    }
    return out.TryPushBack('"');
}

[[nodiscard]] bool AppendUnsigned(DynamicArray<char>& out, u32 value) noexcept
{
    char text[16] = {};
    const int written = std::snprintf(text, sizeof(text), "%u", static_cast<unsigned>(value));
    if (written <= 0)
    {
        return false;
    }
    for (int index = 0; index < written; ++index)
    {
        if (!out.TryPushBack(text[index]))
        {
            return false;
        }
    }
    return true;
}

/// Cooked payload kind implied by a source container. A glTF document is a whole
/// scene; an OBJ file is a single mesh.
[[nodiscard]] AssetKind KindForFormat(SourceFormat format) noexcept
{
    switch (format)
    {
    case SourceFormat::Gltf:
    case SourceFormat::Glb:
        return AssetKind::Scene;
    case SourceFormat::Obj:
        return AssetKind::Mesh;
    case SourceFormat::Png:
    case SourceFormat::Jpeg:
    case SourceFormat::Dds:
    case SourceFormat::Ktx2:
        return AssetKind::Texture;
    case SourceFormat::CyberMaterial:
        return AssetKind::Material;
    case SourceFormat::Unknown:
        return AssetKind::Unknown;
    }
    return AssetKind::Unknown;
}

/// True when a path segment carries no characters. A name of only spaces is not
/// addressable on every platform, so it is treated as absent.
[[nodiscard]] bool IsBlankSegment(StringView segment) noexcept
{
    for (usize index = 0; index < segment.Size(); ++index)
    {
        const char value = segment[index];
        if (value != ' ' && value != '\t')
        {
            return false;
        }
    }
    return true;
}

/// One dependency edge held as a path until every record exists, so a manifest
/// may list assets and edges in any order.
struct PendingDependency
{
    AssetId asset = kInvalidAssetId;
    StringView path;
};

[[nodiscard]] ImportStatus Failure(ErrorCode code, const char* message) noexcept
{
    return ImportStatus::Err(MakeError(code, message));
}

}  // namespace

AssetDatabase::PathStatus AssetDatabase::NormalizePath(StringView source, String& out) noexcept
{
    out.Clear();

    usize index = 0;
    while (index <= source.Size())
    {
        usize start = index;
        while (index < source.Size() && source[index] != '/' && source[index] != '\\')
        {
            ++index;
        }
        const StringView segment = source.Substr(start, index - start);
        if (segment.Equals(StringView("..")))
        {
            out.Clear();
            return PathStatus::EscapesRoot;
        }
        if (!segment.Empty() && !segment.Equals(StringView(".")) && !IsBlankSegment(segment))
        {
            if (!out.Empty() && !out.TryAppend('/'))
            {
                out.Clear();
                return PathStatus::OutOfMemory;
            }
            if (!out.TryAppend(segment))
            {
                out.Clear();
                return PathStatus::OutOfMemory;
            }
        }
        if (index >= source.Size())
        {
            break;
        }
        ++index;
    }

    return out.Empty() ? PathStatus::Empty : PathStatus::Ok;
}

ImportResult<AssetId> AssetDatabase::Register(const StringView& virtualPath,
                                              SourceFormat format) noexcept
{
    if (format == SourceFormat::Unknown)
    {
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::InvalidArgument, "asset source format is unknown"));
    }

    String normalized(m_allocator);
    switch (NormalizePath(virtualPath, normalized))
    {
    case PathStatus::Ok:
        break;
    case PathStatus::Empty:
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::InvalidArgument, "asset path is empty"));
    case PathStatus::EscapesRoot:
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::InvalidArgument, "asset path must stay inside the content root"));
    case PathStatus::OutOfMemory:
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while registering an asset"));
    }

    const AssetId id = AssetIdFromPath(normalized.View());
    if (const AssetRecord* existing = Find(id))
    {
        if (existing->format != format)
        {
            return ImportResult<AssetId>::Err(MakeError(
                ErrorCode::InvalidArgument, "asset is already registered with another format"));
        }
        return ImportResult<AssetId>::Ok(id);
    }

    AssetRecord* record = m_records.TryEmplaceBack(m_allocator);
    if (record == nullptr)
    {
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while registering an asset"));
    }
    record->id = id;
    record->format = format;
    record->kind = KindForFormat(format);
    if (!record->virtualPath.TryAssign(normalized.View()))
    {
        m_records.PopBack();
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while registering an asset"));
    }
    return ImportResult<AssetId>::Ok(id);
}

ImportResult<AssetId> AssetDatabase::Register(const StringView& virtualPath) noexcept
{
    const SourceFormat format = SourceFormatFromPath(virtualPath);
    if (format == SourceFormat::Unknown)
    {
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::InvalidArgument, "asset extension is not a known source format"));
    }
    return Register(virtualPath, format);
}

ImportResult<AssetId> AssetDatabase::RegisterDerived(const StringView& virtualPath,
                                                     AssetKind kind) noexcept
{
    if (kind == AssetKind::Unknown)
    {
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::InvalidArgument, "derived asset kind is unknown"));
    }

    String normalized(m_allocator);
    switch (NormalizePath(virtualPath, normalized))
    {
    case PathStatus::Ok:
        break;
    case PathStatus::Empty:
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::InvalidArgument, "asset path is empty"));
    case PathStatus::EscapesRoot:
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::InvalidArgument, "asset path must stay inside the content root"));
    case PathStatus::OutOfMemory:
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while registering an asset"));
    }

    const AssetId id = AssetIdFromPath(normalized.View());
    if (const AssetRecord* existing = Find(id))
    {
        if (existing->format != SourceFormat::Unknown || existing->kind != kind)
        {
            return ImportResult<AssetId>::Err(MakeError(
                ErrorCode::InvalidArgument, "asset is already registered with another shape"));
        }
        return ImportResult<AssetId>::Ok(id);
    }

    AssetRecord* record = m_records.TryEmplaceBack(m_allocator);
    if (record == nullptr)
    {
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while registering an asset"));
    }
    record->id = id;
    record->format = SourceFormat::Unknown;
    record->kind = kind;
    if (!record->virtualPath.TryAssign(normalized.View()))
    {
        m_records.PopBack();
        return ImportResult<AssetId>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while registering an asset"));
    }
    return ImportResult<AssetId>::Ok(id);
}

const AssetRecord* AssetDatabase::Find(AssetId id) const noexcept
{
    for (usize index = 0; index < m_records.Size(); ++index)
    {
        if (m_records[index].id == id)
        {
            return &m_records[index];
        }
    }
    return nullptr;
}

const AssetRecord* AssetDatabase::Find(const StringView& virtualPath) const noexcept
{
    String normalized(m_allocator);
    if (NormalizePath(virtualPath, normalized) != PathStatus::Ok)
    {
        return nullptr;
    }
    return Find(AssetIdFromPath(normalized.View()));
}

const AssetRecord* AssetDatabase::At(usize index) const noexcept
{
    return index < m_records.Size() ? &m_records[index] : nullptr;
}

AssetRecord* AssetDatabase::FindMutable(AssetId id) noexcept
{
    for (usize index = 0; index < m_records.Size(); ++index)
    {
        if (m_records[index].id == id)
        {
            return &m_records[index];
        }
    }
    return nullptr;
}

bool AssetDatabase::DependsOn(AssetId asset, AssetId candidate) const noexcept
{
    if (Find(asset) == nullptr)
    {
        return false;
    }

    // Iterative walk: a manifest can describe a deep chain, and recursion there
    // would put the depth of untrusted input on the call stack. The stack is
    // deduplicated as it grows, so the walk is linear in edges and not
    // exponential on diamond graphs.
    DynamicArray<AssetId> stack(m_allocator);
    if (!stack.TryPushBack(asset))
    {
        // Without scratch storage the edge cannot be proven safe, so report a
        // dependency and let the caller reject it instead of forming a cycle.
        return true;
    }

    while (!stack.Empty())
    {
        const AssetId current = stack.Back();
        stack.PopBack();
        const AssetRecord* record = Find(current);
        if (record == nullptr)
        {
            continue;
        }
        for (usize index = 0; index < record->dependencies.Size(); ++index)
        {
            const AssetId dependency = record->dependencies[index];
            if (dependency == candidate)
            {
                return true;
            }
            bool queued = false;
            for (usize pending = 0; pending < stack.Size(); ++pending)
            {
                if (stack[pending] == dependency)
                {
                    queued = true;
                    break;
                }
            }
            if (!queued && !stack.TryPushBack(dependency))
            {
                return true;
            }
        }
    }
    return false;
}

bool AssetDatabase::WouldCycle(AssetId asset, AssetId dependency) const noexcept
{
    return asset == dependency || DependsOn(dependency, asset);
}

ImportStatus AssetDatabase::AddDependency(AssetId asset, AssetId dependency) noexcept
{
    AssetRecord* record = FindMutable(asset);
    if (record == nullptr || Find(dependency) == nullptr)
    {
        return Failure(ErrorCode::InvalidArgument, "dependency edge references a missing asset");
    }
    for (usize index = 0; index < record->dependencies.Size(); ++index)
    {
        if (record->dependencies[index] == dependency)
        {
            return ImportStatus::Ok();
        }
    }
    if (WouldCycle(asset, dependency))
    {
        return Failure(ErrorCode::InvalidArgument, "dependency edge would create a cycle");
    }
    if (!record->dependencies.TryPushBack(dependency))
    {
        return Failure(ErrorCode::OutOfRange, "out of memory while adding a dependency");
    }
    return ImportStatus::Ok();
}

ImportStatus AssetDatabase::SetDependencies(AssetId asset,
                                            Span<const AssetId> dependencies) noexcept
{
    AssetRecord* record = FindMutable(asset);
    if (record == nullptr)
    {
        return Failure(ErrorCode::InvalidArgument, "asset is not registered");
    }
    for (usize index = 0; index < dependencies.Size(); ++index)
    {
        for (usize previous = 0; previous < index; ++previous)
        {
            if (dependencies[previous] == dependencies[index])
            {
                return Failure(ErrorCode::InvalidArgument, "dependency list repeats an asset");
            }
        }
        if (Find(dependencies[index]) == nullptr)
        {
            return Failure(ErrorCode::InvalidArgument,
                           "dependency edge references a missing asset");
        }
        if (WouldCycle(asset, dependencies[index]))
        {
            return Failure(ErrorCode::InvalidArgument, "dependency edge would create a cycle");
        }
    }

    DynamicArray<AssetId> replacement(m_allocator);
    if (!replacement.TryResize(dependencies.Size()))
    {
        return Failure(ErrorCode::OutOfRange, "out of memory while setting dependencies");
    }
    for (usize index = 0; index < dependencies.Size(); ++index)
    {
        replacement[index] = dependencies[index];
    }
    record->dependencies = std::move(replacement);
    return ImportStatus::Ok();
}

ImportStatus AssetDatabase::StoreCooked(AssetId asset, const char* cookedPath, u64 sourceHash,
                                        const AssetImportSettings& settings,
                                        u64 payloadChecksum) noexcept
{
    AssetRecord* record = FindMutable(asset);
    if (record == nullptr)
    {
        return Failure(ErrorCode::InvalidArgument, "asset is not registered");
    }
    if (cookedPath == nullptr || cookedPath[0] == '\0')
    {
        return Failure(ErrorCode::InvalidArgument, "cooked path is empty");
    }
    if (!record->cookedPath.TryAssign(StringView(cookedPath)))
    {
        return Failure(ErrorCode::OutOfRange, "out of memory while storing a cooked asset");
    }
    record->sourceHash = sourceHash;
    record->settingsHash = ImportSettingsHash(settings);
    record->cookedChecksum = payloadChecksum;
    return ImportStatus::Ok();
}

bool AssetDatabase::TryGetCooked(AssetId asset, u64 sourceHash, const AssetImportSettings& settings,
                                 CookCacheEntry& entry) const noexcept
{
    const AssetRecord* record = Find(asset);
    if (record == nullptr || !record->IsCooked())
    {
        return false;
    }
    const u64 settingsHash = ImportSettingsHash(settings);
    if (record->sourceHash != sourceHash || record->settingsHash != settingsHash)
    {
        return false;
    }
    entry.sourceHash = record->sourceHash;
    entry.settingsHash = record->settingsHash;
    entry.checksum = record->cookedChecksum;
    return true;
}

void AssetDatabase::Invalidate(AssetId asset) noexcept
{
    for (usize index = 0; index < m_records.Size(); ++index)
    {
        AssetRecord& record = m_records[index];
        if (record.id != asset && !DependsOn(record.id, asset))
        {
            continue;
        }
        record.cookedPath.Clear();
        record.cookedChecksum = 0;
        record.sourceHash = 0;
        record.settingsHash = 0;
    }
}

bool AssetDatabase::CookOrder(DynamicArray<AssetId>& order) const noexcept
{
    order.Clear();
    const usize count = m_records.Size();

    // Kahn's algorithm over dependency counts. Every asset is emitted only after
    // all of its dependencies, and a remainder at the end proves a cycle.
    DynamicArray<u32> remaining(m_allocator);
    DynamicArray<AssetId> ready(m_allocator);
    if (!remaining.TryResize(count))
    {
        return false;
    }
    for (usize index = 0; index < count; ++index)
    {
        remaining[index] = static_cast<u32>(m_records[index].dependencies.Size());
    }
    for (usize index = 0; index < count; ++index)
    {
        if (remaining[index] == 0 && !ready.TryPushBack(m_records[index].id))
        {
            return false;
        }
    }

    usize head = 0;
    while (head < ready.Size())
    {
        const AssetId current = ready[head];
        ++head;
        if (!order.TryPushBack(current))
        {
            return false;
        }
        for (usize index = 0; index < count; ++index)
        {
            const AssetRecord& record = m_records[index];
            if (remaining[index] == 0)
            {
                continue;
            }
            bool depends = false;
            for (usize dependency = 0; dependency < record.dependencies.Size(); ++dependency)
            {
                if (record.dependencies[dependency] == current)
                {
                    depends = true;
                    break;
                }
            }
            if (!depends)
            {
                continue;
            }
            --remaining[index];
            if (remaining[index] == 0 && !ready.TryPushBack(record.id))
            {
                return false;
            }
        }
    }

    return order.Size() == count;
}

void AssetDatabase::Clear() noexcept
{
    m_records.Clear();
}

ImportStatus AssetDatabase::Save(const char* utf8Path) const noexcept
{
    if (utf8Path == nullptr)
    {
        return Failure(ErrorCode::InvalidArgument, "manifest path is null");
    }

    DynamicArray<char> text(m_allocator);
    if (!AppendRaw(text, "{\n  \"format\": ") ||
        !AppendJsonString(text, StringView(kManifestFormatTag)) ||
        !AppendRaw(text, ",\n  \"version\": ") || !AppendUnsigned(text, kAssetDatabaseVersion) ||
        !AppendRaw(text, ",\n  \"assets\": ["))
    {
        return Failure(ErrorCode::OutOfRange, "out of memory while writing the manifest");
    }

    for (usize index = 0; index < m_records.Size(); ++index)
    {
        const AssetRecord& record = m_records[index];
        char sourceHash[kHashHexLength + 1] = {};
        char settingsHash[kHashHexLength + 1] = {};
        char checksum[kHashHexLength + 1] = {};
        FormatHash(record.sourceHash, sourceHash);
        FormatHash(record.settingsHash, settingsHash);
        FormatHash(record.cookedChecksum, checksum);

        if (!AppendRaw(text,
                       index == 0 ? "\n    {\n      \"path\": " : ",\n    {\n      \"path\": ") ||
            !AppendJsonString(text, record.virtualPath.View()) ||
            !AppendRaw(text, ",\n      \"format\": ") ||
            !AppendJsonString(text, record.format == SourceFormat::Unknown
                                        ? StringView("derived")
                                        : StringView(SourceFormatName(record.format))) ||
            !AppendRaw(text, ",\n      \"kind\": ") ||
            !AppendJsonString(text, StringView(AssetKindName(record.kind))) ||
            !AppendRaw(text, ",\n      \"sourceHash\": ") ||
            !AppendJsonString(text, StringView(sourceHash)) ||
            !AppendRaw(text, ",\n      \"settingsHash\": ") ||
            !AppendJsonString(text, StringView(settingsHash)) ||
            !AppendRaw(text, ",\n      \"cookedPath\": ") ||
            !AppendJsonString(text, record.cookedPath.View()) ||
            !AppendRaw(text, ",\n      \"checksum\": ") ||
            !AppendJsonString(text, StringView(checksum)) ||
            !AppendRaw(text, ",\n      \"dependencies\": ["))
        {
            return Failure(ErrorCode::OutOfRange, "out of memory while writing the manifest");
        }

        for (usize dependency = 0; dependency < record.dependencies.Size(); ++dependency)
        {
            const AssetRecord* target = Find(record.dependencies[dependency]);
            if (target == nullptr)
            {
                return Failure(ErrorCode::Internal, "manifest has a dangling dependency");
            }
            if (dependency > 0 && !AppendRaw(text, ", "))
            {
                return Failure(ErrorCode::OutOfRange, "out of memory while writing the manifest");
            }
            if (!AppendJsonString(text, target->virtualPath.View()))
            {
                return Failure(ErrorCode::OutOfRange, "out of memory while writing the manifest");
            }
        }

        if (!AppendRaw(text, "]\n    }"))
        {
            return Failure(ErrorCode::OutOfRange, "out of memory while writing the manifest");
        }
    }

    if (!AppendRaw(text, "\n  ]\n}\n"))
    {
        return Failure(ErrorCode::OutOfRange, "out of memory while writing the manifest");
    }

    const auto write = cyber::platform::WriteFile(
        utf8Path, Span<const Byte>(reinterpret_cast<const Byte*>(text.Data()), text.Size()));
    if (write.IsError())
    {
        return Failure(write.Error().code, write.Error().message);
    }
    return ImportStatus::Ok();
}

ImportStatus AssetDatabase::Load(const char* utf8Path) noexcept
{
    if (utf8Path == nullptr)
    {
        return Failure(ErrorCode::InvalidArgument, "manifest path is null");
    }
    auto file = cyber::platform::ReadFile(utf8Path, m_allocator);
    if (file.IsError())
    {
        return Failure(file.Error().code, file.Error().message);
    }

    auto document = JsonDocument::Parse(
        StringView(reinterpret_cast<const char*>(file.Value().Data()), file.Value().Size()),
        m_allocator);
    if (document.IsError())
    {
        return ImportStatus::Err(document.Error());
    }

    const JsonValue root = document.Value().Root();
    if (!root.IsObject())
    {
        return Failure(ErrorCode::ParseError, "manifest root is not an object");
    }

    StringView formatTag;
    if (!root.Member(StringView("format")).TryGetString(formatTag) ||
        !formatTag.Equals(StringView(kManifestFormatTag)))
    {
        return Failure(ErrorCode::ParseError, "manifest format tag does not match");
    }
    u32 version = 0;
    if (!root.Member(StringView("version")).TryGetU32(version))
    {
        return Failure(ErrorCode::ParseError, "manifest has no version");
    }
    if (version != kAssetDatabaseVersion)
    {
        return Failure(ErrorCode::Unsupported, "manifest version is not supported");
    }

    const JsonValue assets = root.Member(StringView("assets"));
    if (!assets.IsArray())
    {
        return Failure(ErrorCode::ParseError, "manifest has no asset array");
    }

    // Load into a scratch database and adopt its records only after the whole
    // document verifies, so a bad manifest cannot leave a half-populated one.
    AssetDatabase loaded(m_allocator);
    DynamicArray<PendingDependency> edges(m_allocator);

    for (usize index = 0; index < assets.Size(); ++index)
    {
        const JsonValue entry = assets.Element(index);
        if (!entry.IsObject())
        {
            return Failure(ErrorCode::ParseError, "manifest asset entry is not an object");
        }

        StringView path;
        StringView formatName;
        if (!entry.Member(StringView("path")).TryGetString(path) ||
            !entry.Member(StringView("format")).TryGetString(formatName))
        {
            return Failure(ErrorCode::ParseError, "manifest asset entry has no path or format");
        }

        ImportResult<AssetId> registered = ImportResult<AssetId>::Err(
            MakeError(ErrorCode::ParseError, "manifest asset entry is malformed"));
        if (formatName.Equals(StringView("derived")))
        {
            StringView kindName;
            const AssetKind kind = entry.Member(StringView("kind")).TryGetString(kindName)
                                       ? AssetKindFromName(kindName)
                                       : AssetKind::Unknown;
            if (kind == AssetKind::Unknown)
            {
                return Failure(ErrorCode::ParseError, "manifest derived asset has no usable kind");
            }
            registered = loaded.RegisterDerived(path, kind);
        }
        else
        {
            const SourceFormat format = SourceFormatFromName(formatName);
            if (format == SourceFormat::Unknown)
            {
                return Failure(ErrorCode::ParseError, "manifest names an unknown source format");
            }
            registered = loaded.Register(path, format);
        }
        if (registered.IsError())
        {
            return ImportStatus::Err(registered.Error());
        }
        const AssetId id = registered.Value();

        u64 sourceHash = 0;
        u64 settingsHash = 0;
        u64 checksum = 0;
        StringView hashText;
        if (entry.Member(StringView("sourceHash")).TryGetString(hashText) &&
            !ParseHash(hashText, sourceHash))
        {
            return Failure(ErrorCode::ParseError, "manifest source hash is malformed");
        }
        if (entry.Member(StringView("settingsHash")).TryGetString(hashText) &&
            !ParseHash(hashText, settingsHash))
        {
            return Failure(ErrorCode::ParseError, "manifest settings hash is malformed");
        }
        if (entry.Member(StringView("checksum")).TryGetString(hashText) &&
            !ParseHash(hashText, checksum))
        {
            return Failure(ErrorCode::ParseError, "manifest checksum is malformed");
        }

        StringView cookedPath;
        if (entry.Member(StringView("cookedPath")).TryGetString(cookedPath) && !cookedPath.Empty())
        {
            AssetRecord* record = loaded.FindMutable(id);
            if (record == nullptr || !record->cookedPath.TryAssign(cookedPath))
            {
                return Failure(ErrorCode::OutOfRange, "out of memory while loading the manifest");
            }
            record->sourceHash = sourceHash;
            record->settingsHash = settingsHash;
            record->cookedChecksum = checksum;
        }

        const JsonValue dependencies = entry.Member(StringView("dependencies"));
        if (!dependencies.IsArray())
        {
            continue;
        }
        for (usize dependency = 0; dependency < dependencies.Size(); ++dependency)
        {
            StringView dependencyPath;
            if (!dependencies.Element(dependency).TryGetString(dependencyPath))
            {
                return Failure(ErrorCode::ParseError, "manifest dependency is not a string");
            }
            if (!edges.TryPushBack(PendingDependency{id, dependencyPath}))
            {
                return Failure(ErrorCode::OutOfRange, "out of memory while loading the manifest");
            }
        }
    }

    // Edges are applied after every record exists, and each one is checked
    // against the graph built so far, so an out-of-order manifest still cannot
    // introduce a cycle.
    for (usize index = 0; index < edges.Size(); ++index)
    {
        const AssetRecord* target = loaded.Find(edges[index].path);
        if (target == nullptr)
        {
            return Failure(ErrorCode::ParseError,
                           "manifest references a dependency that is not registered");
        }
        const ImportStatus status = loaded.AddDependency(edges[index].asset, target->id);
        if (status.IsError())
        {
            return status;
        }
    }

    m_records = std::move(loaded.m_records);
    return ImportStatus::Ok();
}

}  // namespace cyber::assets
