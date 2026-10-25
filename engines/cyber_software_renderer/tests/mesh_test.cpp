#include <cyber/memory/heap_allocator.h>
#include <cyber/software/mesh.h>
#include <cyber/test/test.h>

#include <cstring>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::f32;
using cyber::core::usize;

namespace
{

cyber::core::Result<Mesh, cyber::core::Error> Load(const char* text, HeapAllocator& heap)
{
    return LoadObjFromMemory(text, std::strlen(text), heap);
}

}  // namespace

CG_TEST(software_mesh_obj_triangle)
{
    HeapAllocator heap;
    const char obj[] = "v 0 0 0\n"
                       "v 1 0 0\n"
                       "v 0 1 0\n"
                       "vt 0 0\n"
                       "vt 1 0\n"
                       "vt 0 1\n"
                       "vn 0 0 1\n"
                       "f 1/1/1 2/2/1 3/3/1\n";

    auto loaded = Load(obj, heap);
    CG_REQUIRE(static_cast<bool>(loaded));
    Mesh mesh = std::move(loaded.Value());
    CG_EXPECT_EQ(mesh.TriangleCount(), usize{1});
    CG_EXPECT_EQ(mesh.VertexCount(), usize{3});

    CG_EXPECT_NEAR(mesh.vertices[0].position.x, 0.0f, 1.0e-6f);
    CG_EXPECT_NEAR(mesh.vertices[1].position.x, 1.0f, 1.0e-6f);
    CG_EXPECT_NEAR(mesh.vertices[2].position.y, 1.0f, 1.0e-6f);
    CG_EXPECT_NEAR(mesh.vertices[1].uv.x, 1.0f, 1.0e-6f);
    CG_EXPECT_NEAR(mesh.vertices[0].normal.z, 1.0f, 1.0e-6f);
}

CG_TEST(software_mesh_obj_quad_is_fan_triangulated)
{
    HeapAllocator heap;
    const char obj[] = "v 0 0 0\n"
                       "v 1 0 0\n"
                       "v 1 1 0\n"
                       "v 0 1 0\n"
                       "f 1 2 3 4\n";

    auto loaded = Load(obj, heap);
    CG_REQUIRE(static_cast<bool>(loaded));
    Mesh mesh = std::move(loaded.Value());
    CG_EXPECT_EQ(mesh.TriangleCount(), usize{2});
    CG_EXPECT_EQ(mesh.VertexCount(), usize{6});

    // Missing normals default to +Z and missing UVs to the origin.
    CG_EXPECT_NEAR(mesh.vertices[0].normal.z, 1.0f, 1.0e-6f);
    CG_EXPECT_NEAR(mesh.vertices[0].uv.x, 0.0f, 1.0e-6f);
}

CG_TEST(software_mesh_obj_negative_indices)
{
    HeapAllocator heap;
    const char obj[] = "v 0 0 0\n"
                       "v 1 0 0\n"
                       "v 0 1 0\n"
                       "f -3 -2 -1\n";

    auto loaded = Load(obj, heap);
    CG_REQUIRE(static_cast<bool>(loaded));
    Mesh mesh = std::move(loaded.Value());
    CG_EXPECT_EQ(mesh.TriangleCount(), usize{1});
    CG_EXPECT_NEAR(mesh.vertices[2].position.y, 1.0f, 1.0e-6f);
}

CG_TEST(software_mesh_obj_rejects_bad_input)
{
    HeapAllocator heap;

    // An out-of-range index fails instead of reading out of bounds.
    const char badIndex[] = "v 0 0 0\n"
                            "v 1 0 0\n"
                            "f 1 2 9\n";
    CG_EXPECT_FALSE(static_cast<bool>(Load(badIndex, heap)));

    // A face with fewer than three corners is malformed.
    const char shortFace[] = "v 0 0 0\n"
                             "v 1 0 0\n"
                             "f 1 2\n";
    CG_EXPECT_FALSE(static_cast<bool>(Load(shortFace, heap)));

    // A malformed number fails the load.
    const char badNumber[] = "v 0 0 nope\n";
    CG_EXPECT_FALSE(static_cast<bool>(Load(badNumber, heap)));

    // A null pointer with a non-zero size is rejected.
    CG_EXPECT_FALSE(static_cast<bool>(LoadObjFromMemory(nullptr, 4, heap)));

    // An empty file is a valid empty mesh.
    auto empty = LoadObjFromMemory("", 0, heap);
    CG_REQUIRE(static_cast<bool>(empty));
    CG_EXPECT_EQ(empty.Value().VertexCount(), usize{0});
}
