#pragma once

#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/memory/allocator.h>
#include <cyber/memory/dynamic_array.h>
#include <cyber/software/shading.h>

namespace cyber::software
{

using cyber::core::usize;

// A triangle-list mesh in object space.
//
// `vertices` holds three vertices per triangle, already expanded from any
// triangulation the loader performed, so the Phase 8 and 9 pipelines consume it
// directly. Positions, normals, and UVs are object space; a transform shader
// moves them.
struct Mesh
{
    memory::DynamicArray<VertexInput> vertices;

    explicit Mesh(memory::Allocator& allocator) noexcept : vertices(allocator) {}
    Mesh(Mesh&&) noexcept = default;
    Mesh& operator=(Mesh&&) noexcept = default;
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    [[nodiscard]] usize VertexCount() const noexcept
    {
        return vertices.Size();
    }

    // Complete triangles in the list.
    [[nodiscard]] usize TriangleCount() const noexcept
    {
        return vertices.Size() / 3;
    }
};

// Parses a Wavefront OBJ file held in memory.
//
// Reads `v`, `vn`, and `vt` records and `f` faces whose corners are `v`,
// `v/vt`, `v//vn`, or `v/vt/vn`. Faces with more than three corners are
// fan-triangulated. Negative indices count back from the most recent record.
// Missing normals default to +Z and missing UVs to (0, 0). Other records are
// ignored.
//
// Input is treated as hostile: the parser never reads past `size`, caps a
// record at 4096 bytes and a face at 64 corners, and returns InvalidArgument
// for a malformed number, an out-of-range index, or a face with fewer than
// three corners. It returns Internal when an allocation fails.
[[nodiscard]] core::Result<Mesh, core::Error>
LoadObjFromMemory(const char* text, usize size, memory::Allocator& allocator) noexcept;

// Reads a file through cyber_platform and parses it with `LoadObjFromMemory`.
[[nodiscard]] core::Result<Mesh, core::Error> LoadObjFile(const char* utf8Path,
                                                          memory::Allocator& allocator) noexcept;

}  // namespace cyber::software
