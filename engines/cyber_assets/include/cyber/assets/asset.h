#pragma once

#include <cyber/core/error.h>
#include <cyber/core/hash.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>
#include <cyber/memory/string_view.h>

namespace cyber::assets
{

using cyber::core::ErrorCode;
using cyber::core::f32;
using cyber::core::f64;
using cyber::core::u16;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::u8;
using cyber::core::usize;

/// Domain diagnostic. Importers keep static message text so an error value never
/// owns storage and can cross a module boundary cheaply. Exactly one of the
/// location fields is meaningful for a given format: text formats fill
/// `line`/`column` (1-based, zero when unknown) and binary formats fill
/// `offset` (zero when unknown).
struct ImportError
{
    ErrorCode code = ErrorCode::Internal;
    const char* message = nullptr;
    u32 line = 0;
    u32 column = 0;
    u64 offset = 0;
};

template <typename T>
using ImportResult = cyber::core::Result<T, ImportError>;

using ImportStatus = cyber::core::Status<ImportError>;

/// Constructs a diagnostic with no location.
[[nodiscard]] inline ImportError MakeError(ErrorCode code, const char* message) noexcept
{
    ImportError error;
    error.code = code;
    error.message = message;
    return error;
}

/// Constructs a byte-offset diagnostic for a binary format.
[[nodiscard]] inline ImportError MakeOffsetError(ErrorCode code, const char* message,
                                                 u64 byteOffset) noexcept
{
    ImportError error = MakeError(code, message);
    error.offset = byteOffset;
    return error;
}

/// Constructs a line/column diagnostic for a text format.
[[nodiscard]] inline ImportError MakeTextError(ErrorCode code, const char* message, u32 line,
                                               u32 column) noexcept
{
    ImportError error = MakeError(code, message);
    error.line = line;
    error.column = column;
    return error;
}

/// Cooked payload families. The value is written into cooked headers, so the
/// numbering is part of the file format.
enum class AssetKind : u16
{
    Unknown = 0,
    Mesh = 1,
    Texture = 2,
    Material = 3,
    Scene = 4,
};

/// Source containers this module can read. Values are recorded in the asset
/// database and therefore stable.
enum class SourceFormat : u16
{
    Unknown = 0,
    Gltf = 1,
    Glb = 2,
    Obj = 3,
    Png = 4,
    Jpeg = 5,
    Dds = 6,
    Ktx2 = 7,
    CyberMaterial = 8,
};

[[nodiscard]] constexpr const char* AssetKindName(AssetKind kind) noexcept
{
    switch (kind)
    {
    case AssetKind::Unknown:
        return "unknown";
    case AssetKind::Mesh:
        return "mesh";
    case AssetKind::Texture:
        return "texture";
    case AssetKind::Material:
        return "material";
    case AssetKind::Scene:
        return "scene";
    }
    return "unknown";
}

/// Maps an `AssetKindName` value back to its enum. Case-insensitive; unknown
/// text yields `AssetKind::Unknown`.
[[nodiscard]] AssetKind AssetKindFromName(cyber::memory::StringView name) noexcept;

[[nodiscard]] constexpr const char* SourceFormatName(SourceFormat format) noexcept
{
    switch (format)
    {
    case SourceFormat::Unknown:
        return "unknown";
    case SourceFormat::Gltf:
        return "gltf";
    case SourceFormat::Glb:
        return "glb";
    case SourceFormat::Obj:
        return "obj";
    case SourceFormat::Png:
        return "png";
    case SourceFormat::Jpeg:
        return "jpeg";
    case SourceFormat::Dds:
        return "dds";
    case SourceFormat::Ktx2:
        return "ktx2";
    case SourceFormat::CyberMaterial:
        return "cybermat";
    }
    return "unknown";
}

/// Detects a source format from a file path extension. Matching is
/// case-insensitive and does not touch the file system.
[[nodiscard]] SourceFormat SourceFormatFromPath(cyber::memory::StringView path) noexcept;

/// Maps a `SourceFormatName` value back to its enum. Case-insensitive; unknown
/// text yields `SourceFormat::Unknown`. Manifest readers use this so the format
/// stays readable in the file instead of encoding an enum number.
[[nodiscard]] SourceFormat SourceFormatFromName(cyber::memory::StringView name) noexcept;

/// True for formats whose cooked payload is a mesh.
[[nodiscard]] constexpr bool IsMeshFormat(SourceFormat format) noexcept
{
    return format == SourceFormat::Gltf || format == SourceFormat::Glb ||
           format == SourceFormat::Obj;
}

/// True for formats whose cooked payload is a texture.
[[nodiscard]] constexpr bool IsTextureFormat(SourceFormat format) noexcept
{
    return format == SourceFormat::Png || format == SourceFormat::Jpeg ||
           format == SourceFormat::Dds || format == SourceFormat::Ktx2;
}

/// Globally unique, deterministic asset identity. IDs are derived from the
/// normalized virtual path so a reference stays valid when the bytes behind it
/// change; content hashes live in the asset database separately.
using AssetId = u64;

inline constexpr AssetId kInvalidAssetId = 0;

/// Salt for the path hash. Changing it invalidates every stored reference, so it
/// is part of the format contract.
inline constexpr u64 kAssetIdSeed = 0x4359424552415353ull;  // "CYBERASS"

[[nodiscard]] AssetId AssetIdFromPath(cyber::memory::StringView virtualPath) noexcept;

/// Resolves `relativePath` against the directory containing `documentPath`,
/// collapsing `.` and `..` segments and normalizing separators to '/'. Both
/// inputs are virtual paths, not file-system paths. Returns false when the
/// result would escape the content root or when either path has more than 64
/// segments, so a document can never reference outside its package.
[[nodiscard]] bool ResolveVirtualPath(cyber::memory::StringView documentPath,
                                      cyber::memory::StringView relativePath,
                                      cyber::memory::String& out) noexcept;

/// Seed for the source content hash. Part of the cook cache key: changing it
/// invalidates every cached payload.
inline constexpr u64 kSourceHashSeed = 0x534F555243454461ull;  // "SOURCEDa"

/// xxHash64 of a source file's bytes. The cooker and the runtime must agree on
/// this value, so the seed lives here instead of at either call site.
[[nodiscard]] u64 HashSourceBytes(cyber::memory::Span<const cyber::core::Byte> bytes) noexcept;

/// Import settings participate in the cook cache key. Two cooks with different
/// settings are different outputs even when the source bytes match.
struct AssetImportSettings
{
    /// Generate tangents for meshes that do not provide them.
    bool generateTangents = true;
    /// Flip the V texture coordinate to match a bottom-left texture origin.
    bool flipTexCoordV = false;
    /// Decode block-compressed textures to RGBA8 instead of keeping the blocks.
    bool decompressTextures = false;

    [[nodiscard]] friend constexpr bool operator==(const AssetImportSettings&,
                                                   const AssetImportSettings&) noexcept = default;
};

[[nodiscard]] u64 ImportSettingsHash(const AssetImportSettings& settings) noexcept;

}  // namespace cyber::assets
