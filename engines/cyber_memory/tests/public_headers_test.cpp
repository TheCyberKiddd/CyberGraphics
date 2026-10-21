#include <cyber/memory/memory.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::memory;

static_assert(sizeof(Handle<void>) == 8);
static_assert(sizeof(Handle<int>) == 8);
static_assert(alignof(Handle<void>) == 4);
static_assert(std::is_standard_layout_v<Handle<void>>);
static_assert(std::is_standard_layout_v<AllocatorStats>);
static_assert(sizeof(Span<int>) == sizeof(void*) + sizeof(usize));

CG_TEST(memory_public_headers_compile_together)
{
    CG_EXPECT_TRUE(IsPowerOfTwo(64));
    CG_EXPECT_FALSE(IsPowerOfTwo(0));
    CG_EXPECT_EQ(AlignUp(17, 8), usize{24});
    CG_EXPECT_TRUE(IsAligned(64, 16));

    const StringView view("memory");
    CG_EXPECT_EQ(view.Size(), usize{6});
    CG_EXPECT_TRUE(view.StartsWith("mem"));

    int values[2] = {1, 2};
    const Span<int> span(values);
    CG_EXPECT_EQ(span.Size(), usize{2});

    const Handle<void> handle;
    CG_EXPECT_FALSE(handle.IsValid());
}
