#include <cyber/assets/asset.h>

#include <cstring>

namespace cyber::assets
{
namespace
{

using cyber::core::HashCombine;
using cyber::core::u8;

[[nodiscard]] char ToLowerAscii(char value) noexcept
{
    return (value >= 'A' && value <= 'Z') ? static_cast<char>(value - 'A' + 'a') : value;
}

/// Compares an extension with a lowercase literal, ignoring ASCII case.
[[nodiscard]] bool ExtensionEquals(cyber::memory::StringView extension,
                                   const char* literal) noexcept
{
    const usize length = std::strlen(literal);
    if (extension.Size() != length)
    {
        return false;
    }
    for (usize index = 0; index < length; ++index)
    {
        if (ToLowerAscii(extension[index]) != literal[index])
        {
            return false;
        }
    }
    return true;
}

}  // namespace

SourceFormat SourceFormatFromPath(cyber::memory::StringView path) noexcept
{
    usize dot = path.Size();
    for (usize index = path.Size(); index > 0; --index)
    {
        const char value = path[index - 1];
        if (value == '.')
        {
            dot = index - 1;
            break;
        }
        if (value == '/' || value == '\\')
        {
            break;
        }
    }
    if (dot >= path.Size())
    {
        return SourceFormat::Unknown;
    }

    const cyber::memory::StringView extension = path.Substr(dot + 1);
    if (ExtensionEquals(extension, "gltf"))
    {
        return SourceFormat::Gltf;
    }
    if (ExtensionEquals(extension, "glb"))
    {
        return SourceFormat::Glb;
    }
    if (ExtensionEquals(extension, "obj"))
    {
        return SourceFormat::Obj;
    }
    if (ExtensionEquals(extension, "png"))
    {
        return SourceFormat::Png;
    }
    if (ExtensionEquals(extension, "jpg") || ExtensionEquals(extension, "jpeg"))
    {
        return SourceFormat::Jpeg;
    }
    if (ExtensionEquals(extension, "dds"))
    {
        return SourceFormat::Dds;
    }
    if (ExtensionEquals(extension, "ktx2"))
    {
        return SourceFormat::Ktx2;
    }
    if (ExtensionEquals(extension, "cybermat"))
    {
        return SourceFormat::CyberMaterial;
    }
    return SourceFormat::Unknown;
}

AssetKind AssetKindFromName(cyber::memory::StringView name) noexcept
{
    if (ExtensionEquals(name, "mesh"))
    {
        return AssetKind::Mesh;
    }
    if (ExtensionEquals(name, "texture"))
    {
        return AssetKind::Texture;
    }
    if (ExtensionEquals(name, "material"))
    {
        return AssetKind::Material;
    }
    if (ExtensionEquals(name, "scene"))
    {
        return AssetKind::Scene;
    }
    return AssetKind::Unknown;
}

SourceFormat SourceFormatFromName(cyber::memory::StringView name) noexcept
{
    if (ExtensionEquals(name, "gltf"))
    {
        return SourceFormat::Gltf;
    }
    if (ExtensionEquals(name, "glb"))
    {
        return SourceFormat::Glb;
    }
    if (ExtensionEquals(name, "obj"))
    {
        return SourceFormat::Obj;
    }
    if (ExtensionEquals(name, "png"))
    {
        return SourceFormat::Png;
    }
    if (ExtensionEquals(name, "jpeg"))
    {
        return SourceFormat::Jpeg;
    }
    if (ExtensionEquals(name, "dds"))
    {
        return SourceFormat::Dds;
    }
    if (ExtensionEquals(name, "ktx2"))
    {
        return SourceFormat::Ktx2;
    }
    if (ExtensionEquals(name, "cybermat"))
    {
        return SourceFormat::CyberMaterial;
    }
    return SourceFormat::Unknown;
}

AssetId AssetIdFromPath(cyber::memory::StringView virtualPath) noexcept
{
    // Normalize the path the same way on every platform: lowercase, forward
    // slashes, no leading "./", no duplicate separators, no trailing separator.
    // Identical logical paths therefore produce identical IDs on Windows and
    // Linux, which keeps cooked databases interchangeable.
    u64 hash = kAssetIdSeed;
    u64 word = 0;
    u32 byteCount = 0;
    usize length = 0;
    bool pendingSeparator = false;

    auto feed = [&](char value) noexcept
    {
        word |= static_cast<u64>(static_cast<u8>(value)) << (byteCount * 8u);
        ++byteCount;
        ++length;
        if (byteCount == 8)
        {
            hash = HashCombine(hash, word);
            word = 0;
            byteCount = 0;
        }
    };

    usize index = 0;
    if (virtualPath.Size() >= 2 && virtualPath[0] == '.' &&
        (virtualPath[1] == '/' || virtualPath[1] == '\\'))
    {
        index = 2;
    }

    for (; index < virtualPath.Size(); ++index)
    {
        char value = virtualPath[index];
        if (value == '\\')
        {
            value = '/';
        }
        value = ToLowerAscii(value);
        if (value == '/')
        {
            if (length == 0)
            {
                // Leading separators carry no meaning.
                continue;
            }
            pendingSeparator = true;
            continue;
        }
        if (pendingSeparator)
        {
            feed('/');
            pendingSeparator = false;
        }
        feed(value);
    }

    if (byteCount != 0)
    {
        hash = HashCombine(hash, word);
    }
    hash = HashCombine(hash, static_cast<u64>(length));
    // Zero is reserved for "no asset", so the ID space skips it.
    return hash == kInvalidAssetId ? 1 : hash;
}

u64 HashSourceBytes(cyber::memory::Span<const cyber::core::Byte> bytes) noexcept
{
    return cyber::core::HashBytes(bytes.Data(), bytes.Size(), kSourceHashSeed);
}

bool ResolveVirtualPath(cyber::memory::StringView documentPath,
                        cyber::memory::StringView relativePath, cyber::memory::String& out) noexcept
{
    constexpr usize kMaxSegments = 64;
    cyber::memory::StringView segments[kMaxSegments];
    usize count = 0;

    // A path that cannot be represented within the segment budget is rejected
    // instead of quietly truncated, because a truncated path could alias a
    // different asset.
    auto feed = [&](cyber::memory::StringView text) noexcept -> bool
    {
        usize index = 0;
        while (index <= text.Size())
        {
            const usize start = index;
            while (index < text.Size() && text[index] != '/' && text[index] != '\\')
            {
                ++index;
            }
            const cyber::memory::StringView segment = text.Substr(start, index - start);
            if (segment.Empty() || segment.Equals(cyber::memory::StringView(".")))
            {
                // Nothing to carry.
            }
            else if (segment.Equals(cyber::memory::StringView("..")))
            {
                if (count == 0)
                {
                    return false;
                }
                --count;
            }
            else
            {
                if (count == kMaxSegments)
                {
                    return false;
                }
                segments[count++] = segment;
            }
            if (index >= text.Size())
            {
                break;
            }
            ++index;
        }
        return true;
    };

    usize documentDirectory = 0;
    for (usize index = documentPath.Size(); index > 0; --index)
    {
        if (documentPath[index - 1] == '/' || documentPath[index - 1] == '\\')
        {
            documentDirectory = index - 1;
            break;
        }
    }
    if (!feed(documentPath.Substr(0, documentDirectory)) || !feed(relativePath))
    {
        out.Clear();
        return false;
    }

    out.Clear();
    for (usize index = 0; index < count; ++index)
    {
        if (index > 0 && !out.TryAppend('/'))
        {
            out.Clear();
            return false;
        }
        if (!out.TryAppend(segments[index]))
        {
            out.Clear();
            return false;
        }
    }
    return !out.Empty();
}

u64 ImportSettingsHash(const AssetImportSettings& settings) noexcept
{
    u64 hash = 0x494d504f52545345ull;  // "IMPORTS"
    hash = cyber::core::HashCombine(hash, settings.generateTangents ? 1u : 0u);
    hash = cyber::core::HashCombine(hash, settings.flipTexCoordV ? 1u : 0u);
    hash = cyber::core::HashCombine(hash, settings.decompressTextures ? 1u : 0u);
    return hash;
}

}  // namespace cyber::assets
