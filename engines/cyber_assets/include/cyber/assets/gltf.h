#pragma once

#include <cyber/assets/asset.h>
#include <cyber/assets/material.h>
#include <cyber/assets/mesh_data.h>
#include <cyber/core/types.h>
#include <cyber/math/mat4.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>

#include <utility>

namespace cyber::assets
{

using cyber::core::Byte;
using cyber::core::i32;
using cyber::core::u32;
using cyber::core::usize;
using cyber::memory::DynamicArray;
using cyber::memory::String;

/// One glTF image. External images keep their relative URI as their identity;
/// embedded images get a synthetic `<document>#image<N>` path so a reference is
/// still stable within a document.
struct ImportedImage
{
    explicit ImportedImage(Allocator& allocator) noexcept
        : uri(allocator), mimeType(allocator), bytes(allocator)
    {
    }

    ImportedImage(const ImportedImage&) = delete;
    ImportedImage& operator=(const ImportedImage&) = delete;
    ImportedImage(ImportedImage&&) noexcept = default;
    ImportedImage& operator=(ImportedImage&&) noexcept = default;
    ~ImportedImage() = default;

    AssetId id = kInvalidAssetId;
    String uri;
    String mimeType;
    DynamicArray<Byte> bytes;
};

/// One node after the hierarchy has been flattened into world matrices.
struct ImportedNode
{
    explicit ImportedNode(Allocator& allocator) noexcept : name(allocator) {}

    ImportedNode(const ImportedNode&) = delete;
    ImportedNode& operator=(const ImportedNode&) = delete;
    ImportedNode(ImportedNode&&) noexcept = default;
    ImportedNode& operator=(ImportedNode&&) noexcept = default;
    ~ImportedNode() = default;

    String name;
    i32 mesh = -1;
    u32 parent = 0;
    bool hasParent = false;
    cyber::math::Mat4 local = cyber::math::Mat4::Identity();
    cyber::math::Mat4 world = cyber::math::Mat4::Identity();
};

/// A drawable placement: which mesh to draw and where.
struct ImportedInstance
{
    u32 node = 0;
    u32 mesh = 0;
    cyber::math::Mat4 transform = cyber::math::Mat4::Identity();
};

/// Everything an importer produced from one document. Meshes are in glTF mesh
/// order, materials in material order, images in image order.
class ImportedScene
{
public:
    explicit ImportedScene(Allocator& allocator) noexcept
        : meshes(allocator), materials(allocator), images(allocator), nodes(allocator),
          instances(allocator)
    {
    }

    ImportedScene(const ImportedScene&) = delete;
    ImportedScene& operator=(const ImportedScene&) = delete;
    ImportedScene(ImportedScene&&) noexcept = default;
    ImportedScene& operator=(ImportedScene&&) noexcept = default;
    ~ImportedScene() = default;

    DynamicArray<MeshAsset> meshes;
    DynamicArray<MaterialAsset> materials;
    DynamicArray<ImportedImage> images;
    DynamicArray<ImportedNode> nodes;
    DynamicArray<ImportedInstance> instances;

    /// Sum of every mesh's index count divided by three.
    [[nodiscard]] usize TotalTriangleCount() const noexcept;
};

struct GltfImportOptions
{
    AssetImportSettings settings{};
    /// Virtual path used to name embedded images and to seed synthetic IDs.
    const char* documentName = "scene.gltf";
    /// Upper bound on nodes visited in the hierarchy. Guards against cycles.
    u32 maxNodes = 65536;
};

/// Imports a `.gltf` (JSON, with external buffers/images) or `.glb` document.
/// `baseDirectory` resolves relative URIs; pass an empty string for a document
/// that embeds all of its buffers and images.
[[nodiscard]] ImportResult<ImportedScene>
ImportGltf(cyber::memory::Span<const Byte> bytes, const char* baseDirectory, Allocator& allocator,
           const GltfImportOptions& options = {}) noexcept;

/// Reads a `.gltf` or `.glb` file and resolves sibling resources relative to it.
[[nodiscard]] ImportResult<ImportedScene>
ImportGltfFile(const char* utf8Path, Allocator& allocator,
               const GltfImportOptions& options = {}) noexcept;

}  // namespace cyber::assets
