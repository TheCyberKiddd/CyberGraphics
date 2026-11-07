#include <cyber/assets/cooked.h>
#include <cyber/assets/gltf.h>
#include <cyber/core/hash.h>
#include <cyber/platform/filesystem.h>

#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <utility>

namespace cyber::assets
{
namespace
{

using cyber::core::ErrorCode;
using cyber::core::HashBytes;
using cyber::math::Mat4;
using cyber::math::Vec3;
using cyber::math::Vec4;

static_assert(std::endian::native == std::endian::little,
              "cooked payloads are little-endian; this build target is not supported");

/// A section count larger than this cannot be a real asset and would make the
/// table itself hostile input.
constexpr u32 kMaxSections = 32;
/// Upper bound on a single cooked file. Anything larger is rejected before any
/// section is trusted, so a corrupt size cannot request a huge allocation.
constexpr u64 kMaxCookedBytes = 1ull << 32;

[[nodiscard]] u16 ReadU16(const Byte* data) noexcept
{
    return static_cast<u16>(std::to_integer<u8>(data[0])) |
           static_cast<u16>(std::to_integer<u8>(data[1]) << 8u);
}

[[nodiscard]] u32 ReadU32(const Byte* data) noexcept
{
    return static_cast<u32>(std::to_integer<u8>(data[0])) |
           (static_cast<u32>(std::to_integer<u8>(data[1])) << 8u) |
           (static_cast<u32>(std::to_integer<u8>(data[2])) << 16u) |
           (static_cast<u32>(std::to_integer<u8>(data[3])) << 24u);
}

[[nodiscard]] u64 ReadU64(const Byte* data) noexcept
{
    return static_cast<u64>(ReadU32(data)) | (static_cast<u64>(ReadU32(data + 4)) << 32u);
}

[[nodiscard]] bool IsValidKind(u16 value) noexcept
{
    switch (static_cast<AssetKind>(value))
    {
    case AssetKind::Mesh:
    case AssetKind::Texture:
    case AssetKind::Material:
    case AssetKind::Scene:
        return true;
    case AssetKind::Unknown:
        return false;
    }
    return false;
}

class Writer
{
public:
    explicit Writer(DynamicArray<Byte>& out) noexcept : m_out(out) {}

    void U8(u8 value) noexcept
    {
        Push(static_cast<Byte>(value));
    }

    void U16(u16 value) noexcept
    {
        for (u32 shift = 0; shift < 16; shift += 8)
        {
            U8(static_cast<u8>((value >> shift) & 0xffu));
        }
    }

    void U32(u32 value) noexcept
    {
        for (u32 shift = 0; shift < 32; shift += 8)
        {
            U8(static_cast<u8>((value >> shift) & 0xffu));
        }
    }

    void U64(u64 value) noexcept
    {
        for (u32 shift = 0; shift < 64; shift += 8)
        {
            U8(static_cast<u8>((value >> shift) & 0xffu));
        }
    }

    void Raw(const void* data, usize size) noexcept
    {
        const Byte* bytes = static_cast<const Byte*>(data);
        for (usize index = 0; index < size; ++index)
        {
            Push(bytes[index]);
        }
    }

    [[nodiscard]] bool Ok() const noexcept
    {
        return m_ok;
    }

private:
    void Push(Byte value) noexcept
    {
        m_ok = m_ok && m_out.TryPushBack(value);
    }

    DynamicArray<Byte>& m_out;
    bool m_ok = true;
};

/// Access to a typed range inside the mapped bytes, which is also where a
/// misaligned or out-of-range section is caught.
class SectionReader
{
public:
    SectionReader(Span<const Byte> bytes, const CookedSectionEntry& entry) noexcept
        : m_bytes(bytes), m_entry(entry)
    {
    }

    template <typename T>
    [[nodiscard]] Span<const T> AsArray() const noexcept
    {
        const u64 elementBytes = static_cast<u64>(sizeof(T));
        if (m_entry.count > (1ull << 32) / elementBytes ||
            m_entry.size != m_entry.count * elementBytes)
        {
            return Span<const T>();
        }
        if (m_entry.offset > m_bytes.Size() || m_entry.size > m_bytes.Size() - m_entry.offset)
        {
            return Span<const T>();
        }
        if (m_entry.count == 0)
        {
            return Span<const T>();
        }
        const T* data = reinterpret_cast<const T*>(m_bytes.Data() + m_entry.offset);
        if ((reinterpret_cast<usize>(data) % alignof(T)) != 0)
        {
            return Span<const T>();
        }
        return Span<const T>(data, static_cast<usize>(m_entry.count));
    }

    [[nodiscard]] u64 Count() const noexcept
    {
        return m_entry.count;
    }

    [[nodiscard]] Span<const Byte> AsBytes() const noexcept
    {
        if (m_entry.offset > m_bytes.Size() || m_entry.size > m_bytes.Size() - m_entry.offset)
        {
            return Span<const Byte>();
        }
        return m_bytes.Subspan(static_cast<usize>(m_entry.offset),
                               static_cast<usize>(m_entry.size));
    }

    /// Reads a single trailer-compatible record. Returns false when the section
    /// is too small, which is a format error rather than an empty value.
    template <typename T>
    [[nodiscard]] bool AsRecord(T& out) const noexcept
    {
        if (m_entry.size < sizeof(T) || m_entry.offset > m_bytes.Size() ||
            sizeof(T) > m_bytes.Size() - m_entry.offset)
        {
            return false;
        }
        std::memcpy(&out, m_bytes.Data() + m_entry.offset, sizeof(T));
        return true;
    }

private:
    Span<const Byte> m_bytes;
    CookedSectionEntry m_entry;
};

/// Collects sections and assembles the final file: header, table, aligned
/// payload, checksum.
class Builder
{
public:
    explicit Builder(Allocator& allocator) noexcept
        : m_allocator(allocator), m_sections(allocator), m_strings(allocator)
    {
    }

    [[nodiscard]] bool AddSection(CookedSectionId id, u64 count, const void* data,
                                  usize size) noexcept
    {
        if (m_sections.Size() >= kMaxSections)
        {
            return false;
        }
        if (!m_sections.TryEmplaceBack(m_allocator))
        {
            return false;
        }
        Section& section = m_sections[m_sections.Size() - 1];
        section.id = static_cast<u32>(id);
        section.count = count;
        if (size == 0)
        {
            return true;
        }
        if (!section.bytes.TryResize(size))
        {
            return false;
        }
        std::memcpy(section.bytes.Data(), data, size);
        return true;
    }

    template <typename T>
    [[nodiscard]] bool AddArray(CookedSectionId id, Span<const T> values) noexcept
    {
        return AddSection(id, static_cast<u64>(values.Size()), values.Data(),
                          values.Size() * sizeof(T));
    }

    /// Appends a name to the shared string table and returns its byte offset.
    [[nodiscard]] bool AddString(cyber::memory::StringView text, u32& offset, u32& length) noexcept
    {
        if (text.Size() > 0xffffffffull || m_strings.Size() + text.Size() > 0xffffffffull)
        {
            return false;
        }
        offset = static_cast<u32>(m_strings.Size());
        length = static_cast<u32>(text.Size());
        if (text.Empty())
        {
            return true;
        }
        const usize previous = m_strings.Size();
        if (!m_strings.TryResize(previous + text.Size()))
        {
            return false;
        }
        std::memcpy(m_strings.Data() + previous, text.Data(), text.Size());
        return true;
    }

    [[nodiscard]] ImportResult<DynamicArray<Byte>> Finish(AssetKind kind, AssetId id,
                                                          u64 sourceHash, u64 settingsHash) noexcept
    {
        if (!AddSection(CookedSectionId::Strings, m_strings.Size(), m_strings.Data(),
                        m_strings.Size()))
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory while cooking"));
        }

        // Offsets are computed first so the table can be written before any
        // section body, and each section starts on a 16-byte boundary.
        const u64 tableBytes = TableBytes();
        u64 cursor = (kCookedHeaderBytes + tableBytes + kCookedSectionAlignment - 1) &
                     ~static_cast<u64>(kCookedSectionAlignment - 1);
        const usize offsetCount = m_sections.Size();
        DynamicArray<u64> offsets(m_allocator);
        if (!offsets.TryResize(offsetCount))
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory while cooking"));
        }
        for (usize index = 0; index < offsetCount; ++index)
        {
            offsets[index] = cursor;
            cursor += static_cast<u64>(m_sections[index].bytes.Size());
            cursor = (cursor + kCookedSectionAlignment - 1) &
                     ~static_cast<u64>(kCookedSectionAlignment - 1);
        }
        if (cursor > kMaxCookedBytes)
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::Overflow, "cooked asset exceeds the size limit"));
        }

        DynamicArray<Byte> bytes(m_allocator);
        if (!bytes.TryResize(static_cast<usize>(cursor)))
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory while cooking"));
        }
        std::memset(bytes.Data(), 0, bytes.Size());

        DynamicArray<Byte> headerBytes(m_allocator);
        Writer header(headerBytes);
        header.U32(kCookedMagic);
        header.U16(kCookedFormatVersion);
        header.U16(static_cast<u16>(kind));
        header.U32(kCookedHeaderBytes);
        header.U32(kCookedFlagLittleEndian);
        header.U64(id);
        header.U64(sourceHash);
        header.U64(settingsHash);
        header.U64(cursor - kCookedHeaderBytes);
        header.U64(0);  // checksum, patched below
        header.U32(static_cast<u32>(offsetCount));
        header.U32(0);
        if (!header.Ok() || headerBytes.Size() != kCookedHeaderBytes)
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::Internal, "cooked header could not be built"));
        }
        std::memcpy(bytes.Data(), headerBytes.Data(), kCookedHeaderBytes);

        DynamicArray<Byte> tableBytesArray(m_allocator);
        Writer table(tableBytesArray);
        for (usize index = 0; index < offsetCount; ++index)
        {
            table.U32(m_sections[index].id);
            table.U32(0);
            table.U64(offsets[index]);
            table.U64(static_cast<u64>(m_sections[index].bytes.Size()));
            table.U64(m_sections[index].count);
        }
        if (!table.Ok())
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory while cooking"));
        }
        std::memcpy(bytes.Data() + kCookedHeaderBytes, tableBytesArray.Data(),
                    tableBytesArray.Size());

        for (usize index = 0; index < offsetCount; ++index)
        {
            const Section& section = m_sections[index];
            if (!section.bytes.Empty())
            {
                std::memcpy(bytes.Data() + offsets[index], section.bytes.Data(),
                            section.bytes.Size());
            }
        }

        const u64 checksum = HashBytes(bytes.Data() + kCookedHeaderBytes,
                                       bytes.Size() - kCookedHeaderBytes, kCookedChecksumSeed);
        DynamicArray<Byte> checksumBytes(m_allocator);
        Writer checksumWriter(checksumBytes);
        checksumWriter.U64(checksum);
        if (!checksumWriter.Ok())
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory while cooking"));
        }
        std::memcpy(bytes.Data() + 48, checksumBytes.Data(), sizeof(u64));

        return ImportResult<DynamicArray<Byte>>::Ok(std::move(bytes));
    }

private:
    [[nodiscard]] u64 TableBytes() const noexcept
    {
        return static_cast<u64>(m_sections.Size()) * kCookedSectionBytes;
    }

    struct Section
    {
        explicit Section(Allocator& allocator) noexcept : id(0), count(0), bytes(allocator) {}

        u32 id;
        u64 count;
        DynamicArray<Byte> bytes;
    };

    Allocator& m_allocator;
    DynamicArray<Section> m_sections;
    DynamicArray<Byte> m_strings;
};

[[nodiscard]] bool AddInfoSection(Builder& builder, CookedSectionId id, const void* record,
                                  usize size) noexcept
{
    return builder.AddSection(id, 1, record, size);
}

[[nodiscard]] ImportResult<CookedAsset> ParseFailure(ErrorCode code, const char* message) noexcept
{
    return ImportResult<CookedAsset>::Err(MakeError(code, message));
}

}  // namespace

ImportResult<DynamicArray<Byte>> CookMesh(const MeshAsset& mesh, AssetId id, u64 sourceHash,
                                          const AssetImportSettings& settings,
                                          Allocator& allocator) noexcept
{
    auto checked = mesh.Check();
    if (checked.IsError())
    {
        return ImportResult<DynamicArray<Byte>>::Err(checked.Error());
    }
    if (mesh.bounds.IsEmpty())
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::InvalidArgument, "cooked mesh needs computed bounds"));
    }

    Builder builder(allocator);
    if (!builder.AddArray(CookedSectionId::MeshVertices,
                          Span<const MeshVertex>(mesh.vertices.Data(), mesh.vertices.Size())) ||
        !builder.AddArray(CookedSectionId::MeshIndices,
                          Span<const u32>(mesh.indices.Data(), mesh.indices.Size())) ||
        !builder.AddArray(CookedSectionId::Submeshes,
                          Span<const SubmeshAsset>(mesh.submeshes.Data(), mesh.submeshes.Size())))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the mesh"));
    }

    CookedMeshInfo info;
    info.boundsMinimum = mesh.bounds.Min();
    info.boundsMaximum = mesh.bounds.Max();
    if (!builder.AddString(mesh.name.View(), info.nameOffset, info.nameLength))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the mesh"));
    }
    if (!AddInfoSection(builder, CookedSectionId::MeshInfo, &info, sizeof(info)))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the mesh"));
    }

    return builder.Finish(AssetKind::Mesh, id, sourceHash, ImportSettingsHash(settings));
}

ImportResult<DynamicArray<Byte>> CookTexture(const TextureAsset& texture, AssetId id,
                                             u64 sourceHash, const AssetImportSettings& settings,
                                             Allocator& allocator) noexcept
{
    auto validated = texture.Validate();
    if (validated.IsError())
    {
        return ImportResult<DynamicArray<Byte>>::Err(validated.Error());
    }

    Builder builder(allocator);
    if (!builder.AddSection(CookedSectionId::TextureData, texture.data.Size(), texture.data.Data(),
                            texture.data.Size()) ||
        !builder.AddArray(CookedSectionId::TextureLevels,
                          Span<const TextureLevel>(texture.levels.Data(), texture.levels.Size())))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the texture"));
    }

    CookedTextureInfo info;
    info.format = static_cast<u16>(texture.format);
    info.width = texture.width;
    info.height = texture.height;
    info.depth = texture.depth;
    info.mipCount = texture.mipCount;
    info.layerCount = texture.layerCount;
    info.cubemap = texture.cubemap ? 1u : 0u;
    if (!builder.AddString(texture.name.View(), info.nameOffset, info.nameLength))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the texture"));
    }
    if (!AddInfoSection(builder, CookedSectionId::TextureInfo, &info, sizeof(info)))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the texture"));
    }

    return builder.Finish(AssetKind::Texture, id, sourceHash, ImportSettingsHash(settings));
}

namespace
{

/// Copies a validated material into the cooked record and appends its name to the
/// builder's string table. Both the standalone material and the scene material
/// table use this, so the two cannot drift apart.
[[nodiscard]] bool MakeMaterialRecord(const MaterialAsset& material, Builder& builder,
                                      CookedMaterialRecord& record,
                                      CookedTextureReference references[kCookedTextureSlots])
{
    record = CookedMaterialRecord{};
    record.baseColorFactor = material.baseColorFactor;
    record.emissiveFactor = material.emissiveFactor;
    record.metallicFactor = material.metallicFactor;
    record.roughnessFactor = material.roughnessFactor;
    record.normalScale = material.normalScale;
    record.occlusionStrength = material.occlusionStrength;
    record.alphaCutoff = material.alphaCutoff;
    record.alphaMode = static_cast<u32>(material.alphaMode);
    record.doubleSided = material.doubleSided ? 1u : 0u;
    if (!builder.AddString(material.name.View(), record.nameOffset, record.nameLength))
    {
        return false;
    }

    const TextureReference* slots[kCookedTextureSlots] = {
        &material.baseColorTexture, &material.metallicRoughnessTexture, &material.normalTexture,
        &material.occlusionTexture, &material.emissiveTexture,
    };
    for (u32 slot = 0; slot < kCookedTextureSlots; ++slot)
    {
        references[slot].image = slots[slot]->image;
        references[slot].texCoord = slots[slot]->texCoord;
        references[slot].sampler = slots[slot]->sampler;
    }
    return true;
}

}  // namespace

ImportResult<DynamicArray<Byte>> CookMaterial(const MaterialAsset& material, AssetId id,
                                              u64 sourceHash, const AssetImportSettings& settings,
                                              Allocator& allocator) noexcept
{
    auto validated = material.Validate();
    if (validated.IsError())
    {
        return ImportResult<DynamicArray<Byte>>::Err(validated.Error());
    }

    Builder builder(allocator);
    CookedMaterialRecord record;
    CookedTextureReference references[kCookedTextureSlots];
    if (!MakeMaterialRecord(material, builder, record, references))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the material"));
    }

    // The record and its texture references share one section so a material is
    // always a single lookup.
    DynamicArray<Byte> section(allocator);
    const usize sectionSize = sizeof(record) + sizeof(references);
    if (!section.TryResize(sectionSize))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the material"));
    }
    std::memcpy(section.Data(), &record, sizeof(record));
    std::memcpy(section.Data() + sizeof(record), references, sizeof(references));
    if (!builder.AddSection(CookedSectionId::Material, 1 + kCookedTextureSlots, section.Data(),
                            section.Size()))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the material"));
    }

    return builder.Finish(AssetKind::Material, id, sourceHash, ImportSettingsHash(settings));
}

ImportResult<DynamicArray<Byte>> CookScene(const ImportedScene& scene, AssetId id, u64 sourceHash,
                                           const AssetImportSettings& settings,
                                           Allocator& allocator, const char* documentName) noexcept
{
    const char* document = documentName == nullptr ? "scene.gltf" : documentName;
    Builder builder(allocator);

    DynamicArray<AssetId> meshIds(allocator);
    if (!meshIds.TryResize(scene.meshes.Size()))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
    }
    for (usize index = 0; index < meshIds.Size(); ++index)
    {
        char synthetic[192] = {};
        const int written =
            std::snprintf(synthetic, sizeof(synthetic), "%s#mesh%zu", document, index);
        if (written <= 0 || static_cast<usize>(written) >= sizeof(synthetic))
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::OutOfRange, "cooked scene name is too long"));
        }
        meshIds[index] =
            AssetIdFromPath(cyber::memory::StringView(synthetic, static_cast<usize>(written)));
    }

    DynamicArray<CookedSceneNode> nodes(allocator);
    if (!nodes.TryResize(scene.nodes.Size()))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
    }
    for (usize index = 0; index < nodes.Size(); ++index)
    {
        const ImportedNode& source = scene.nodes[index];
        CookedSceneNode& node = nodes[index];
        node.local = source.local;
        node.world = source.world;
        node.parent = source.hasParent ? source.parent : 0;
        node.flags = (source.hasParent ? kCookedNodeHasParent : 0);
        if (source.mesh >= 0)
        {
            const usize meshIndex = static_cast<usize>(source.mesh);
            if (meshIndex >= meshIds.Size())
            {
                return ImportResult<DynamicArray<Byte>>::Err(
                    MakeError(ErrorCode::Internal, "cooked scene node references a missing mesh"));
            }
            node.mesh = meshIds[meshIndex];
            node.flags |= kCookedNodeHasMesh;
        }
        if (!builder.AddString(source.name.View(), node.nameOffset, node.nameLength))
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
        }
    }

    // Materials are embedded in the scene payload: a submesh only carries an
    // index, so the scene must ship the table that index addresses.
    DynamicArray<CookedMaterialRecord> materials(allocator);
    DynamicArray<CookedTextureReference> materialTextures(allocator);
    if (!materials.TryResize(scene.materials.Size()) ||
        !materialTextures.TryResize(scene.materials.Size() * kCookedTextureSlots))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
    }
    for (usize index = 0; index < materials.Size(); ++index)
    {
        const MaterialAsset& material = scene.materials[index];
        const ImportStatus validated = material.Validate();
        if (validated.IsError())
        {
            return ImportResult<DynamicArray<Byte>>::Err(validated.Error());
        }
        if (!MakeMaterialRecord(material, builder, materials[index],
                                materialTextures.Data() + index * kCookedTextureSlots))
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
        }
    }

    DynamicArray<CookedSceneInstance> instances(allocator);
    if (!instances.TryResize(scene.instances.Size()))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
    }
    for (usize index = 0; index < instances.Size(); ++index)
    {
        const ImportedInstance& source = scene.instances[index];
        if (source.mesh >= meshIds.Size())
        {
            return ImportResult<DynamicArray<Byte>>::Err(
                MakeError(ErrorCode::Internal, "cooked scene instance references a missing mesh"));
        }
        CookedSceneInstance& instance = instances[index];
        instance.transform = source.transform;
        instance.mesh = meshIds[source.mesh];
        instance.node = source.node;
    }

    if (!builder.AddArray(CookedSectionId::SceneNodes,
                          Span<const CookedSceneNode>(nodes.Data(), nodes.Size())) ||
        !builder.AddArray(CookedSectionId::SceneInstances,
                          Span<const CookedSceneInstance>(instances.Data(), instances.Size())) ||
        !builder.AddArray(CookedSectionId::SceneMaterials,
                          Span<const CookedMaterialRecord>(materials.Data(), materials.Size())) ||
        !builder.AddArray(
            CookedSectionId::SceneMaterialTextures,
            Span<const CookedTextureReference>(materialTextures.Data(), materialTextures.Size())))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
    }

    // The info record and the mesh id table are separate sections so the reader
    // finds them by id rather than by position.
    CookedSceneInfo info;
    info.nodeCount = static_cast<u32>(nodes.Size());
    info.instanceCount = static_cast<u32>(instances.Size());
    info.meshCount = static_cast<u32>(meshIds.Size());
    info.materialCount = static_cast<u32>(materials.Size());
    if (!AddInfoSection(builder, CookedSectionId::SceneInfo, &info, sizeof(info)))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
    }
    if (!builder.AddArray(CookedSectionId::SceneMeshes,
                          Span<const AssetId>(meshIds.Data(), meshIds.Size())))
    {
        return ImportResult<DynamicArray<Byte>>::Err(
            MakeError(ErrorCode::OutOfRange, "out of memory while cooking the scene"));
    }

    return builder.Finish(AssetKind::Scene, id, sourceHash, ImportSettingsHash(settings));
}

ImportResult<CookedAsset> CookedAsset::FromBytes(Span<const Byte> bytes) noexcept
{
    return Parse(bytes);
}

ImportResult<CookedFile> CookedFile::Load(const char* utf8Path, Allocator& allocator) noexcept
{
    auto file = cyber::platform::ReadFile(utf8Path, allocator);
    if (file.IsError())
    {
        return ImportResult<CookedFile>::Err(MakeError(file.Error().code, file.Error().message));
    }

    CookedFile cooked(allocator);
    cooked.m_bytes = std::move(file.Value());
    auto parsed =
        CookedAsset::Parse(Span<const Byte>(cooked.m_bytes.Data(), cooked.m_bytes.Size()));
    if (parsed.IsError())
    {
        return ImportResult<CookedFile>::Err(parsed.Error());
    }
    // The views alias m_bytes, whose block is never relocated once the file has
    // been read, so the asset stays valid as the CookedFile is moved.
    cooked.m_asset = parsed.Value();
    return ImportResult<CookedFile>::Ok(std::move(cooked));
}

ImportResult<CookedAsset> CookedAsset::Parse(Span<const Byte> bytes) noexcept
{
    if (bytes.Size() < kCookedHeaderBytes)
    {
        return ParseFailure(ErrorCode::ParseError, "cooked file is smaller than its header");
    }
    if (bytes.Size() > kMaxCookedBytes)
    {
        return ParseFailure(ErrorCode::OutOfRange, "cooked file exceeds the size limit");
    }

    const Byte* data = bytes.Data();
    CookedHeader header;
    header.magic = ReadU32(data);
    header.formatVersion = ReadU16(data + 4);
    header.kind = ReadU16(data + 6);
    header.headerBytes = ReadU32(data + 8);
    header.flags = ReadU32(data + 12);
    header.id = ReadU64(data + 16);
    header.sourceHash = ReadU64(data + 24);
    header.settingsHash = ReadU64(data + 32);
    header.payloadBytes = ReadU64(data + 40);
    header.payloadChecksum = ReadU64(data + 48);
    header.sectionCount = ReadU32(data + 56);
    header.reserved = ReadU32(data + 60);

    if (header.magic != kCookedMagic)
    {
        return ParseFailure(ErrorCode::ParseError, "cooked magic does not match");
    }
    if (header.formatVersion != kCookedFormatVersion)
    {
        return ParseFailure(ErrorCode::Unsupported, "cooked format version is not supported");
    }
    if (!IsValidKind(header.kind))
    {
        return ParseFailure(ErrorCode::ParseError, "cooked asset kind is not known");
    }
    if (header.headerBytes != kCookedHeaderBytes || header.reserved != 0)
    {
        return ParseFailure(ErrorCode::ParseError, "cooked header is malformed");
    }
    if (header.flags != kCookedFlagLittleEndian)
    {
        return ParseFailure(ErrorCode::Unsupported, "cooked header flags are not supported");
    }
    if (header.sectionCount == 0 || header.sectionCount > kMaxSections)
    {
        return ParseFailure(ErrorCode::ParseError, "cooked section count is invalid");
    }
    if (header.payloadBytes != bytes.Size() - kCookedHeaderBytes)
    {
        return ParseFailure(ErrorCode::ParseError, "cooked payload size does not match the file");
    }

    const u64 tableBytes = static_cast<u64>(header.sectionCount) * kCookedSectionBytes;
    if (tableBytes > bytes.Size() - kCookedHeaderBytes)
    {
        return ParseFailure(ErrorCode::ParseError, "cooked section table overruns the file");
    }

    const u64 checksum = HashBytes(data + kCookedHeaderBytes, (bytes.Size() - kCookedHeaderBytes),
                                   kCookedChecksumSeed);
    if (checksum != header.payloadChecksum)
    {
        return ParseFailure(ErrorCode::ParseError, "cooked payload checksum does not match");
    }

    auto findSection = [&](CookedSectionId id) noexcept
    {
        for (u32 index = 0; index < header.sectionCount; ++index)
        {
            const Byte* entry =
                data + kCookedHeaderBytes + static_cast<usize>(index) * kCookedSectionBytes;
            if (ReadU32(entry) != static_cast<u32>(id))
            {
                continue;
            }
            CookedSectionEntry section;
            section.id = ReadU32(entry);
            section.reserved = ReadU32(entry + 4);
            section.offset = ReadU64(entry + 8);
            section.size = ReadU64(entry + 16);
            section.count = ReadU64(entry + 24);
            const u64 firstPayloadByte = kCookedHeaderBytes + tableBytes;
            if (section.reserved != 0 || section.offset < firstPayloadByte ||
                (section.offset % kCookedSectionAlignment) != 0 || section.size > bytes.Size() ||
                section.offset > bytes.Size() - section.size)
            {
                return SectionReader(bytes, CookedSectionEntry{});
            }
            return SectionReader(bytes, section);
        }
        return SectionReader(bytes, CookedSectionEntry{});
    };

    CookedAsset asset;
    asset.m_bytes = bytes;
    asset.m_header = header;
    asset.m_kind = static_cast<AssetKind>(header.kind);

    switch (asset.m_kind)
    {
    case AssetKind::Mesh:
    {
        const SectionReader info = findSection(CookedSectionId::MeshInfo);
        const SectionReader vertices = findSection(CookedSectionId::MeshVertices);
        const SectionReader indices = findSection(CookedSectionId::MeshIndices);
        const SectionReader submeshes = findSection(CookedSectionId::Submeshes);
        const SectionReader strings = findSection(CookedSectionId::Strings);
        asset.m_mesh.vertices = vertices.AsArray<MeshVertex>();
        asset.m_mesh.indices = indices.AsArray<u32>();
        asset.m_mesh.submeshes = submeshes.AsArray<SubmeshAsset>();
        asset.m_mesh.strings = strings.AsBytes();
        if (!info.AsRecord(asset.m_mesh.info))
        {
            return ParseFailure(ErrorCode::ParseError, "cooked mesh info section is missing");
        }
        if (asset.m_mesh.info.nameOffset > asset.m_mesh.strings.Size() ||
            asset.m_mesh.info.nameLength >
                asset.m_mesh.strings.Size() - asset.m_mesh.info.nameOffset)
        {
            return ParseFailure(ErrorCode::ParseError, "cooked mesh name is out of range");
        }
        if (asset.m_mesh.vertices.Empty() || asset.m_mesh.indices.Empty() ||
            asset.m_mesh.submeshes.Empty())
        {
            return ParseFailure(ErrorCode::ParseError, "cooked mesh sections are empty");
        }
        for (usize index = 0; index < asset.m_mesh.submeshes.Size(); ++index)
        {
            const SubmeshAsset& submesh = asset.m_mesh.submeshes[index];
            if (submesh.indexCount == 0 || submesh.indexCount % 3 != 0 ||
                submesh.firstIndex > asset.m_mesh.indices.Size() ||
                submesh.indexCount > asset.m_mesh.indices.Size() - submesh.firstIndex)
            {
                return ParseFailure(ErrorCode::ParseError, "cooked submesh range is invalid");
            }
        }
        break;
    }
    case AssetKind::Texture:
    {
        const SectionReader info = findSection(CookedSectionId::TextureInfo);
        const SectionReader levels = findSection(CookedSectionId::TextureLevels);
        const SectionReader payload = findSection(CookedSectionId::TextureData);
        const SectionReader strings = findSection(CookedSectionId::Strings);
        asset.m_texture.levels = levels.AsArray<TextureLevel>();
        asset.m_texture.data = payload.AsBytes();
        asset.m_texture.strings = strings.AsBytes();
        if (!info.AsRecord(asset.m_texture.info))
        {
            return ParseFailure(ErrorCode::ParseError, "cooked texture info section is missing");
        }
        const auto format = static_cast<TextureFormat>(asset.m_texture.info.format);
        if (TextureBlockBytes(format) == 0 || asset.m_texture.levels.Empty())
        {
            return ParseFailure(ErrorCode::ParseError, "cooked texture format is not supported");
        }
        if (asset.m_texture.info.nameOffset > asset.m_texture.strings.Size() ||
            asset.m_texture.info.nameLength >
                asset.m_texture.strings.Size() - asset.m_texture.info.nameOffset)
        {
            return ParseFailure(ErrorCode::ParseError, "cooked texture name is out of range");
        }
        for (usize index = 0; index < asset.m_texture.levels.Size(); ++index)
        {
            const TextureLevel& level = asset.m_texture.levels[index];
            if (level.size > asset.m_texture.data.Size() ||
                level.offset > asset.m_texture.data.Size() - level.size)
            {
                return ParseFailure(ErrorCode::ParseError, "cooked texture level is out of range");
            }
        }
        break;
    }
    case AssetKind::Material:
    {
        const SectionReader section = findSection(CookedSectionId::Material);
        const SectionReader strings = findSection(CookedSectionId::Strings);
        asset.m_material.strings = strings.AsBytes();
        if (!section.AsRecord(asset.m_material.record))
        {
            return ParseFailure(ErrorCode::ParseError, "cooked material section is missing");
        }
        const usize expectedBytes =
            sizeof(CookedMaterialRecord) + kCookedTextureSlots * sizeof(CookedTextureReference);
        const Span<const Byte> records = section.AsBytes();
        if (section.Count() != 1 + kCookedTextureSlots || records.Size() < expectedBytes)
        {
            return ParseFailure(ErrorCode::ParseError, "cooked material is truncated");
        }
        // The record is 16-byte sized, so the reference array that follows it is
        // 16-byte aligned inside a 16-byte aligned section.
        static_assert(sizeof(CookedMaterialRecord) % alignof(CookedTextureReference) == 0,
                      "material record must keep texture references aligned");
        asset.m_material.textures =
            Span<const CookedTextureReference>(reinterpret_cast<const CookedTextureReference*>(
                                                   records.Data() + sizeof(CookedMaterialRecord)),
                                               kCookedTextureSlots);
        if (asset.m_material.record.nameOffset > asset.m_material.strings.Size() ||
            asset.m_material.record.nameLength >
                asset.m_material.strings.Size() - asset.m_material.record.nameOffset)
        {
            return ParseFailure(ErrorCode::ParseError, "cooked material name is out of range");
        }
        break;
    }
    case AssetKind::Scene:
    {
        const SectionReader info = findSection(CookedSectionId::SceneInfo);
        const SectionReader nodes = findSection(CookedSectionId::SceneNodes);
        const SectionReader instances = findSection(CookedSectionId::SceneInstances);
        const SectionReader meshes = findSection(CookedSectionId::SceneMeshes);
        const SectionReader materials = findSection(CookedSectionId::SceneMaterials);
        const SectionReader materialTextures = findSection(CookedSectionId::SceneMaterialTextures);
        const SectionReader strings = findSection(CookedSectionId::Strings);
        asset.m_scene.nodes = nodes.AsArray<CookedSceneNode>();
        asset.m_scene.instances = instances.AsArray<CookedSceneInstance>();
        asset.m_scene.meshes = meshes.AsArray<AssetId>();
        asset.m_scene.materials = materials.AsArray<CookedMaterialRecord>();
        asset.m_scene.materialTextures = materialTextures.AsArray<CookedTextureReference>();
        asset.m_scene.strings = strings.AsBytes();
        if (!info.AsRecord(asset.m_scene.info))
        {
            return ParseFailure(ErrorCode::ParseError, "cooked scene info section is missing");
        }
        if (asset.m_scene.info.nodeCount != asset.m_scene.nodes.Size() ||
            asset.m_scene.info.instanceCount != asset.m_scene.instances.Size() ||
            asset.m_scene.info.meshCount != asset.m_scene.meshes.Size() ||
            asset.m_scene.info.materialCount != asset.m_scene.materials.Size())
        {
            return ParseFailure(ErrorCode::ParseError, "cooked scene counts do not match");
        }
        if (asset.m_scene.materialTextures.Size() !=
            asset.m_scene.materials.Size() * kCookedTextureSlots)
        {
            return ParseFailure(ErrorCode::ParseError, "cooked scene material slots do not match");
        }
        for (usize index = 0; index < asset.m_scene.materials.Size(); ++index)
        {
            const CookedMaterialRecord& record = asset.m_scene.materials[index];
            if (record.nameOffset > asset.m_scene.strings.Size() ||
                record.nameLength > asset.m_scene.strings.Size() - record.nameOffset)
            {
                return ParseFailure(ErrorCode::ParseError,
                                    "cooked scene material name is out of range");
            }
        }
        break;
    }
    case AssetKind::Unknown:
        return ParseFailure(ErrorCode::ParseError, "cooked asset kind is not known");
    }

    return ImportResult<CookedAsset>::Ok(std::move(asset));
}

}  // namespace cyber::assets
