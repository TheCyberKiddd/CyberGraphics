#pragma once

#include <cyber/assets/asset.h>
#include <cyber/assets/mesh_data.h>
#include <cyber/core/types.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/memory/span.h>
#include <cyber/memory/string.h>

namespace cyber::assets
{

using cyber::core::Byte;
using cyber::memory::DynamicArray;
using cyber::memory::String;

/// Result of importing one OBJ file. OBJ carries no material definitions, only
/// names, so `materialNames` maps `SubmeshAsset::materialIndex` to the `usemtl`
/// name and `dependencies` lists every `mtllib` file the source referenced.
struct ObjImport
{
    explicit ObjImport(Allocator& allocator) noexcept
        : mesh(allocator), materialNames(allocator), dependencies(allocator)
    {
    }

    ObjImport(const ObjImport&) = delete;
    ObjImport& operator=(const ObjImport&) = delete;
    ObjImport(ObjImport&&) noexcept = default;
    ObjImport& operator=(ObjImport&&) noexcept = default;
    ~ObjImport() = default;

    MeshAsset mesh;
    DynamicArray<String> materialNames;
    DynamicArray<String> dependencies;
};

/// Imports an OBJ file from memory. Supports `v`, `vt`, `vn`, `f` with any
/// polygon size (fan triangulated), positive and negative indices, `o`/`g`,
/// `usemtl`, and `mtllib`. Materials are names only; the cooker resolves them.
[[nodiscard]] ImportResult<ObjImport> ImportObj(cyber::memory::Span<const Byte> text,
                                                Allocator& allocator,
                                                const AssetImportSettings& settings = {}) noexcept;

/// Reads and imports an OBJ file. Relative `mtllib` names are recorded as
/// dependencies but are not loaded here.
[[nodiscard]] ImportResult<ObjImport>
ImportObjFile(const char* utf8Path, Allocator& allocator,
              const AssetImportSettings& settings = {}) noexcept;

}  // namespace cyber::assets
