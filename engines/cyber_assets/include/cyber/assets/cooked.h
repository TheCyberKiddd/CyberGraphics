#pragma once

#include <cyber/assets/asset.h>
#include <cyber/assets/material.h>
#include <cyber/assets/mesh_data.h>
#include <cyber/assets/texture.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/math/mat4.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>

#include <utility>

namespace cyber::assets
{

using cyber::core::Byte;
using cyber::core::u16;
using cyber::core::u32;
using cyber::core::u64;
using cyber::core::usize;
using cyber::memory::Allocator;
using cyber::memory::DynamicArray;
using cyber::memory::Span;

/// Cooked payloads are little-endian, versioned, checksummed, and laid out so
/// every section starts on a 16-byte boundary. A loader can map a file and
/// expose the payload without copying: the section table gives typed spans
/// straight into the mapped bytes.
inline constexpr u32 kCookedMagic = 0x53414743u;  // "CGAS" as little-endian bytes
inline constexpr u16 kCookedFormatVersion = 1;
inline constexpr u32 kCookedHeaderBytes = 64;
inline constexpr u32 kCookedSectionBytes = 32;
inline constexpr u32 kCookedSectionAlignment = 16;

/// Seed for the payload checksum. Part of the format contract: changing it
/// invalidates every cooked file.
inline constexpr u64 kCookedChecksumSeed = 0x43474153434F4F4Bull;  // "CGASCOOK"

/// Header flags. Bit 0 records that the payload is little-endian; every other
/// bit must be zero, so a future feature cannot be silently ignored.
inline constexpr u32 kCookedFlagLittleEndian = 1u << 0;

/// Section identifiers. Values are part of the file format; never reuse one.
enum class CookedSectionId : u32
{
    Invalid = 0,
    /// `MeshVertex` array.
    MeshVertices = 1,
    /// `u32` triangle indices.
    MeshIndices = 2,
    /// `SubmeshAsset` array.
    Submeshes = 3,
    /// One `CookedMeshInfo`.
    MeshInfo = 4,
    /// Opaque texture payload addressed by `TextureLevel::offset`.
    TextureData = 5,
    /// `TextureLevel` array.
    TextureLevels = 6,
    /// One `CookedMaterialRecord`.
    Material = 7,
    /// `CookedSceneNode` array.
    SceneNodes = 8,
    /// `CookedSceneInstance` array.
    SceneInstances = 9,
    /// UTF-8 name bytes referenced by offsets.
    Strings = 10,
    /// One `CookedTextureInfo`.
    TextureInfo = 11,
    /// One `CookedSceneInfo`.
    SceneInfo = 12,
    /// `AssetId` array: the meshes a scene references, in document order.
    SceneMeshes = 13,
    /// `CookedMaterialRecord` array: the materials a scene references, in
    /// document order. `SubmeshAsset::materialIndex` indexes it.
    SceneMaterials = 14,
    /// `CookedTextureReference` array: `kCookedTextureSlots` entries per scene
    /// material, in slot order.
    SceneMaterialTextures = 15,
};

[[nodiscard]] constexpr const char* CookedSectionName(CookedSectionId id) noexcept
{
    switch (id)
    {
    case CookedSectionId::Invalid:
        return "invalid";
    case CookedSectionId::MeshVertices:
        return "mesh.vertices";
    case CookedSectionId::MeshIndices:
        return "mesh.indices";
    case CookedSectionId::Submeshes:
        return "submeshes";
    case CookedSectionId::MeshInfo:
        return "mesh.info";
    case CookedSectionId::TextureData:
        return "texture.data";
    case CookedSectionId::TextureLevels:
        return "texture.levels";
    case CookedSectionId::TextureInfo:
        return "texture.info";
    case CookedSectionId::Material:
        return "material";
    case CookedSectionId::SceneNodes:
        return "scene.nodes";
    case CookedSectionId::SceneInstances:
        return "scene.instances";
    case CookedSectionId::SceneInfo:
        return "scene.info";
    case CookedSectionId::SceneMeshes:
        return "scene.meshes";
    case CookedSectionId::SceneMaterials:
        return "scene.materials";
    case CookedSectionId::SceneMaterialTextures:
        return "scene.materialTextures";
    case CookedSectionId::Strings:
        return "strings";
    }
    return "invalid";
}

/// Header fields and section table entry, as stored in the file. These structs
/// are documentation of the on-disk layout; parsing uses an explicit
/// little-endian reader so struct padding can never leak into the format.
struct CookedHeader
{
    u32 magic = kCookedMagic;
    u16 formatVersion = kCookedFormatVersion;
    u16 kind = static_cast<u16>(AssetKind::Unknown);
    u32 headerBytes = kCookedHeaderBytes;
    u32 flags = kCookedFlagLittleEndian;
    AssetId id = kInvalidAssetId;
    /// xxHash64 of the exact source bytes the payload was cooked from.
    u64 sourceHash = 0;
    /// ImportSettingsHash of the settings used, so a settings change misses the
    /// cache instead of silently reusing the old payload.
    u64 settingsHash = 0;
    /// Bytes after the header: section table plus section payloads.
    u64 payloadBytes = 0;
    /// xxHash64 of those bytes, computed with kCookedChecksumSeed.
    u64 payloadChecksum = 0;
    u32 sectionCount = 0;
    u32 reserved = 0;
};

struct CookedSectionEntry
{
    u32 id = 0;
    u32 reserved = 0;
    /// Byte offset from the start of the file.
    u64 offset = 0;
    u64 size = 0;
    /// Element count, for sections whose payload is a typed array.
    u64 count = 0;
};

static_assert(sizeof(CookedHeader) == kCookedHeaderBytes, "cooked header size changed");
static_assert(sizeof(CookedSectionEntry) == kCookedSectionBytes, "cooked section size changed");

/// Fixed-size mesh record. Geometry lives in the vertex, index, and submesh
/// sections; bounds are stored so a loader never has to walk the vertices.
struct CookedMeshInfo
{
    cyber::math::Vec3 boundsMinimum{};
    cyber::math::Vec3 boundsMaximum{};
    u32 nameOffset = 0;
    u32 nameLength = 0;
};

/// Fixed-size texture record.
struct CookedTextureInfo
{
    u16 format = 0;
    u16 reserved = 0;
    u32 width = 0;
    u32 height = 1;
    u32 depth = 1;
    u32 mipCount = 1;
    u32 layerCount = 1;
    u32 cubemap = 0;
    u32 nameOffset = 0;
    u32 nameLength = 0;
};

/// Fixed-size material record. Texture references follow it in the same
/// section, five `CookedTextureReference` entries in slot order.
struct CookedMaterialRecord
{
    cyber::math::Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    cyber::math::Vec3 emissiveFactor{};
    f32 metallicFactor = 1.0f;
    f32 roughnessFactor = 1.0f;
    f32 normalScale = 1.0f;
    f32 occlusionStrength = 1.0f;
    f32 alphaCutoff = 0.5f;
    u32 alphaMode = 0;
    u32 doubleSided = 0;
    u32 nameOffset = 0;
    u32 nameLength = 0;
    u32 reserved = 0;
};

struct CookedTextureReference
{
    AssetId image = kInvalidAssetId;
    u32 texCoord = 0;
    u32 sampler = 0;
};

/// Material slot order in the cooked stream. Stable because cookers and
/// loaders index it.
inline constexpr u32 kCookedTextureSlots = 5;

/// One node after flattening: local and world matrices plus the parent link.
struct CookedSceneNode
{
    cyber::math::Mat4 local{};
    cyber::math::Mat4 world{};
    u64 mesh = kInvalidAssetId;
    u32 parent = 0;
    u32 flags = 0;
    u32 nameOffset = 0;
    u32 nameLength = 0;
};

/// Bit 0 of `CookedSceneNode::flags` marks a node that has a parent; bit 1
/// marks a node that draws a mesh (`mesh` is then a valid asset id).
inline constexpr u32 kCookedNodeHasParent = 1u << 0;
inline constexpr u32 kCookedNodeHasMesh = 1u << 1;

/// A drawable placement inside a cooked scene.
struct CookedSceneInstance
{
    cyber::math::Mat4 transform{};
    u64 mesh = kInvalidAssetId;
    u32 node = 0;
    u32 reserved = 0;
};

struct CookedSceneInfo
{
    u32 nodeCount = 0;
    u32 instanceCount = 0;
    u32 meshCount = 0;
    u32 materialCount = 0;
};

/// Zero-copy view of a cooked mesh. The spans point into the bytes the view was
/// produced from; the caller owns those bytes and must keep them alive.
struct CookedMeshView
{
    CookedMeshInfo info{};
    Span<const MeshVertex> vertices;
    Span<const u32> indices;
    Span<const SubmeshAsset> submeshes;
    Span<const Byte> strings;
};

/// Zero-copy view of a cooked texture.
struct CookedTextureView
{
    CookedTextureInfo info{};
    Span<const TextureLevel> levels;
    /// Raw payload addressed by `TextureLevel::offset`.
    Span<const Byte> data;
    Span<const Byte> strings;
};

/// Zero-copy view of a cooked material.
struct CookedMaterialView
{
    CookedMaterialRecord record{};
    Span<const CookedTextureReference> textures;
    Span<const Byte> strings;
};

/// Zero-copy view of a cooked scene.
struct CookedSceneView
{
    CookedSceneInfo info{};
    Span<const CookedSceneNode> nodes;
    Span<const CookedSceneInstance> instances;
    /// Asset IDs of the meshes referenced by cooked indices, in order.
    Span<const AssetId> meshes;
    /// Scene materials in document order. A submesh's material index points here.
    Span<const CookedMaterialRecord> materials;
    /// `kCookedTextureSlots` references per material, in slot order.
    Span<const CookedTextureReference> materialTextures;
    Span<const Byte> strings;
};

/// Cooks one imported mesh into a self-contained cooked file.
[[nodiscard]] ImportResult<DynamicArray<Byte>> CookMesh(const MeshAsset& mesh, AssetId id,
                                                        u64 sourceHash,
                                                        const AssetImportSettings& settings,
                                                        Allocator& allocator) noexcept;

/// Cooks one imported texture into a self-contained cooked file.
[[nodiscard]] ImportResult<DynamicArray<Byte>> CookTexture(const TextureAsset& texture, AssetId id,
                                                           u64 sourceHash,
                                                           const AssetImportSettings& settings,
                                                           Allocator& allocator) noexcept;

/// Cooks one imported material into a self-contained cooked file.
[[nodiscard]] ImportResult<DynamicArray<Byte>> CookMaterial(const MaterialAsset& material,
                                                            AssetId id, u64 sourceHash,
                                                            const AssetImportSettings& settings,
                                                            Allocator& allocator) noexcept;

/// Cooks an imported scene. Mesh and material references become asset IDs
/// derived from `documentName` and the glTF index, so a scene cooked without
/// its meshes still references them consistently.
[[nodiscard]] ImportResult<DynamicArray<Byte>>
CookScene(const class ImportedScene& scene, AssetId id, u64 sourceHash,
          const AssetImportSettings& settings, Allocator& allocator,
          const char* documentName = "scene.gltf") noexcept;

/// A parsed cooked file whose views alias memory the caller owns. Parsing never
/// allocates and never copies payload bytes; it verifies the header, the section
/// table, and the payload checksum, then hands out typed spans.
class CookedAsset
{
public:
    CookedAsset() noexcept = default;

    /// Parses and verifies borrowed bytes. The returned views alias `bytes`, so
    /// the caller must keep them alive for as long as the asset is used.
    [[nodiscard]] static ImportResult<CookedAsset> FromBytes(Span<const Byte> bytes) noexcept;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return m_kind != AssetKind::Unknown;
    }
    [[nodiscard]] AssetKind Kind() const noexcept
    {
        return m_kind;
    }
    [[nodiscard]] AssetId Id() const noexcept
    {
        return m_header.id;
    }
    [[nodiscard]] u64 SourceHash() const noexcept
    {
        return m_header.sourceHash;
    }
    [[nodiscard]] u64 SettingsHash() const noexcept
    {
        return m_header.settingsHash;
    }
    [[nodiscard]] u64 PayloadChecksum() const noexcept
    {
        return m_header.payloadChecksum;
    }
    [[nodiscard]] const CookedHeader& Header() const noexcept
    {
        return m_header;
    }

    /// Views into the owning storage. Empty when the kind does not match.
    [[nodiscard]] const CookedMeshView& AsMesh() const noexcept
    {
        return m_mesh;
    }
    [[nodiscard]] const CookedTextureView& AsTexture() const noexcept
    {
        return m_texture;
    }
    [[nodiscard]] const CookedMaterialView& AsMaterial() const noexcept
    {
        return m_material;
    }
    [[nodiscard]] const CookedSceneView& AsScene() const noexcept
    {
        return m_scene;
    }

    [[nodiscard]] Span<const Byte> Bytes() const noexcept
    {
        return m_bytes;
    }

private:
    friend class CookedFile;

    [[nodiscard]] static ImportResult<CookedAsset> Parse(Span<const Byte> bytes) noexcept;

    Span<const Byte> m_bytes;
    CookedHeader m_header{};
    AssetKind m_kind = AssetKind::Unknown;
    CookedMeshView m_mesh{};
    CookedTextureView m_texture{};
    CookedMaterialView m_material{};
    CookedSceneView m_scene{};
};

/// A cooked file that owns its bytes, for loaders that read from disk. The
/// buffer is read once and never reallocated, so the asset's views stay valid
/// for the lifetime of the file.
class CookedFile
{
public:
    explicit CookedFile(Allocator& allocator) noexcept : m_bytes(allocator), m_asset() {}

    CookedFile(const CookedFile&) = delete;
    CookedFile& operator=(const CookedFile&) = delete;
    CookedFile(CookedFile&&) noexcept = default;
    CookedFile& operator=(CookedFile&&) noexcept = default;
    ~CookedFile() = default;

    /// Reads, verifies, and parses a cooked file.
    [[nodiscard]] static ImportResult<CookedFile> Load(const char* utf8Path,
                                                       Allocator& allocator) noexcept;

    [[nodiscard]] const CookedAsset& Asset() const noexcept
    {
        return m_asset;
    }

private:
    DynamicArray<Byte> m_bytes;
    CookedAsset m_asset;
};

}  // namespace cyber::assets
