#include <cyber/memory/heap_allocator.h>
#include <cyber/software/depth_buffer.h>
#include <cyber/test/test.h>

#include <cmath>
#include <utility>

using namespace cyber::software;
using namespace cyber::memory;
using cyber::core::i32;
using cyber::core::u8;
using cyber::core::usize;

namespace
{

bool CreateDepth(HeapAllocator& heap, i32 width, i32 height, usize strideBytes, DepthBuffer& out)
{
    DepthBufferDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.strideBytes = strideBytes;
    auto created = DepthBuffer::Create(desc, heap);
    if (!created)
    {
        return false;
    }
    out = std::move(created.Value());
    return true;
}

}  // namespace

CG_TEST(software_depth_buffer_create_and_layout)
{
    HeapAllocator heap;
    DepthBuffer depth;
    CG_REQUIRE(CreateDepth(heap, 4, 3, 0, depth));

    CG_EXPECT_TRUE(depth.IsValid());
    CG_EXPECT_EQ(depth.Width(), i32{4});
    CG_EXPECT_EQ(depth.Height(), i32{3});
    CG_EXPECT_EQ(depth.StrideBytes(), usize{16});
    CG_EXPECT_EQ(depth.DataSizeBytes(), usize{48});
    CG_EXPECT_TRUE(depth.Data() != nullptr);
    CG_EXPECT_TRUE(depth.Row(0) != nullptr);
    CG_EXPECT_TRUE(depth.Row(2) != nullptr);
    CG_EXPECT_TRUE(depth.Row(3) == nullptr);
    CG_EXPECT_TRUE(depth.Row(-1) == nullptr);

    // A fresh buffer is cleared to the reverse-Z far value.
    for (i32 y = 0; y < depth.Height(); ++y)
    {
        for (i32 x = 0; x < depth.Width(); ++x)
        {
            CG_EXPECT_EQ(depth.Get(x, y), 0.0f);
        }
    }

    CG_EXPECT_EQ(depth.Get(-1, 0), 0.0f);
    CG_EXPECT_EQ(depth.Get(4, 0), 0.0f);
    CG_EXPECT_EQ(depth.Get(0, 3), 0.0f);
    CG_EXPECT_FALSE(depth.TestAndWrite(4, 0, 0.5f));
    CG_EXPECT_FALSE(depth.TestAndWrite(0, 3, 0.5f));
}

CG_TEST(software_depth_buffer_reverse_z_test)
{
    HeapAllocator heap;
    DepthBuffer depth;
    CG_REQUIRE(CreateDepth(heap, 2, 2, 0, depth));

    depth.Clear(0.0f);
    CG_EXPECT_TRUE(depth.TestAndWrite(0, 0, 0.5f));
    CG_EXPECT_EQ(depth.Get(0, 0), 0.5f);

    // Farther geometry (a smaller reverse-Z value) loses.
    CG_EXPECT_FALSE(depth.TestAndWrite(0, 0, 0.25f));
    CG_EXPECT_EQ(depth.Get(0, 0), 0.5f);

    // Equal depth passes and rewrites the same value.
    CG_EXPECT_TRUE(depth.TestAndWrite(0, 0, 0.5f));
    CG_EXPECT_EQ(depth.Get(0, 0), 0.5f);

    // Nearer geometry wins.
    CG_EXPECT_TRUE(depth.TestAndWrite(0, 0, 1.0f));
    CG_EXPECT_EQ(depth.Get(0, 0), 1.0f);

    // Values below the far plane are rejected against a cleared buffer.
    depth.Clear(0.0f);
    CG_EXPECT_FALSE(depth.TestAndWrite(1, 1, -0.1f));
    CG_EXPECT_EQ(depth.Get(1, 1), 0.0f);

    // NaN never passes.
    CG_EXPECT_FALSE(depth.TestAndWrite(1, 1, std::nanf("")));
    CG_EXPECT_EQ(depth.Get(1, 1), 0.0f);
}

CG_TEST(software_depth_buffer_stride_padding)
{
    HeapAllocator heap;
    {
        DepthBuffer depth;
        CG_REQUIRE(CreateDepth(heap, 2, 2, 16, depth));
        CG_EXPECT_EQ(depth.StrideBytes(), usize{16});
        CG_EXPECT_EQ(depth.DataSizeBytes(), usize{32});

        depth.Clear(0.75f);

        const u8* bytes = reinterpret_cast<const u8*>(depth.Data());
        for (i32 y = 0; y < depth.Height(); ++y)
        {
            const u8* row = bytes + static_cast<usize>(y) * depth.StrideBytes();
            // Clear writes only the two samples of the row; the eight padding
            // bytes keep the zeros written by Create.
            for (usize index = 8; index < 16; ++index)
            {
                CG_EXPECT_EQ(row[index], u8{0});
            }
            CG_EXPECT_EQ(depth.Get(0, y), 0.75f);
            CG_EXPECT_EQ(depth.Get(1, y), 0.75f);
        }
    }

    CG_EXPECT_EQ(heap.Stats().used, usize{0});
}

CG_TEST(software_depth_buffer_rejects_invalid_descriptors)
{
    HeapAllocator heap;

    DepthBufferDesc desc{};
    CG_EXPECT_FALSE(static_cast<bool>(DepthBuffer::Create(desc, heap)));

    desc.width = 4;
    desc.height = 0;
    CG_EXPECT_FALSE(static_cast<bool>(DepthBuffer::Create(desc, heap)));

    desc.width = 16385;
    desc.height = 1;
    CG_EXPECT_FALSE(static_cast<bool>(DepthBuffer::Create(desc, heap)));

    desc.width = 4;
    desc.height = 4;
    desc.strideBytes = 12;
    CG_EXPECT_FALSE(static_cast<bool>(DepthBuffer::Create(desc, heap)));

    desc.strideBytes = 18;
    CG_EXPECT_FALSE(static_cast<bool>(DepthBuffer::Create(desc, heap)));

    desc.strideBytes = 16;
    auto valid = DepthBuffer::Create(desc, heap);
    CG_REQUIRE(static_cast<bool>(valid));

    DepthBuffer moved = std::move(valid.Value());
    CG_EXPECT_TRUE(moved.IsValid());
    CG_EXPECT_EQ(heap.Stats().used, usize{64});

    DepthBuffer assignTarget;
    assignTarget = std::move(moved);
    CG_EXPECT_TRUE(assignTarget.IsValid());
    CG_EXPECT_FALSE(moved.IsValid());
    CG_EXPECT_EQ(heap.Stats().used, usize{64});

    {
        DepthBuffer released = std::move(assignTarget);
        CG_EXPECT_TRUE(released.IsValid());
    }
    CG_EXPECT_EQ(heap.Stats().used, usize{0});
}

CG_TEST(software_depth_buffer_invalid_operations_are_safe)
{
    DepthBuffer invalid;
    CG_EXPECT_FALSE(invalid.IsValid());
    CG_EXPECT_EQ(invalid.Width(), i32{0});
    CG_EXPECT_EQ(invalid.Height(), i32{0});
    CG_EXPECT_EQ(invalid.StrideBytes(), usize{0});
    CG_EXPECT_EQ(invalid.DataSizeBytes(), usize{0});
    CG_EXPECT_TRUE(invalid.Data() == nullptr);
    CG_EXPECT_TRUE(invalid.Row(0) == nullptr);
    invalid.Clear(1.0f);
    CG_EXPECT_FALSE(invalid.TestAndWrite(0, 0, 1.0f));
    CG_EXPECT_EQ(invalid.Get(0, 0), 0.0f);
}
