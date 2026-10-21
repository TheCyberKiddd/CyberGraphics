#include <cyber/memory/memory.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::memory;

CG_TEST(memory_span)
{
    int values[4] = {1, 2, 3, 4};
    Span<int> span(values);
    CG_EXPECT_EQ(span.Size(), usize{4});
    CG_EXPECT_FALSE(span.Empty());
    CG_EXPECT_EQ(span[2], 3);
    CG_EXPECT_EQ(span.Front(), 1);
    CG_EXPECT_EQ(span.Back(), 4);

    const Span<int> middle = span.Subspan(1, 2);
    CG_EXPECT_EQ(middle.Size(), usize{2});
    CG_EXPECT_EQ(middle[0], 2);
    CG_EXPECT_EQ(span.First(2).Size(), usize{2});
    CG_EXPECT_EQ(span.Last(2)[0], 3);
    CG_EXPECT_EQ(span.Subspan(10).Size(), usize{0});

    Span<const int> constSpan = span;
    CG_EXPECT_EQ(constSpan.Size(), usize{4});
    CG_EXPECT_EQ(constSpan[0], 1);

    Span<int> empty;
    CG_EXPECT_TRUE(empty.Empty());
    CG_EXPECT_EQ(empty.Size(), usize{0});
}

CG_TEST(memory_string_view)
{
    const StringView view("hello");
    CG_EXPECT_EQ(view.Size(), usize{5});
    CG_EXPECT_EQ(view[1], 'e');
    CG_EXPECT_TRUE(view.StartsWith("he"));
    CG_EXPECT_TRUE(view.EndsWith("lo"));
    CG_EXPECT_FALSE(view.StartsWith("hello!"));
    CG_EXPECT_EQ(view.Find('l'), usize{2});
    CG_EXPECT_EQ(view.Find('z'), kInvalidIndex);
    CG_EXPECT_TRUE(view.Contains('e'));
    CG_EXPECT_TRUE(view.Substr(1, 3) == StringView("ell"));
    CG_EXPECT_TRUE(view == StringView("hello"));
    CG_EXPECT_TRUE(view != StringView("world"));
    CG_EXPECT_TRUE(view.Hash(0) != 0);
    CG_EXPECT_EQ(view.Hash(0), StringView("hello").Hash(0));

    const StringView nullView(nullptr);
    CG_EXPECT_TRUE(nullView.Empty());
    CG_EXPECT_EQ(StringView("").Size(), usize{0});
}

CG_TEST(memory_dynamic_array)
{
    HeapAllocator heap;
    DynamicArray<int> array(heap);

    for (int value = 0; value < 10; ++value)
    {
        CG_EXPECT_TRUE(array.TryPushBack(value));
    }
    CG_EXPECT_EQ(array.Size(), usize{10});
    CG_EXPECT_TRUE(array.Capacity() >= 10);
    CG_EXPECT_EQ(array[5], 5);
    CG_EXPECT_EQ(array.Front(), 0);
    CG_EXPECT_EQ(array.Back(), 9);
    CG_EXPECT_EQ(array.End() - array.Begin(), static_cast<std::ptrdiff_t>(array.Size()));

    CG_EXPECT_TRUE(array.TryResize(5));
    CG_EXPECT_EQ(array.Size(), usize{5});
    array.PopBack();
    CG_EXPECT_EQ(array.Back(), 3);

    int* inserted = array.TryEmplaceBack(42);
    CG_REQUIRE(inserted != nullptr);
    CG_EXPECT_EQ(*inserted, 42);
    CG_EXPECT_FALSE(array.TryReserve(kInvalidIndex));

    DynamicArray<int> moved(std::move(array));
    CG_EXPECT_EQ(moved.Size(), usize{5});
    CG_EXPECT_TRUE(array.Empty());
    CG_EXPECT_EQ(moved.Back(), 42);

    DynamicArray<int> assigned(heap);
    assigned = std::move(moved);
    CG_EXPECT_EQ(assigned.Size(), usize{5});
    CG_EXPECT_TRUE(moved.Empty());

    assigned.Clear();
    CG_EXPECT_TRUE(assigned.Empty());
}

CG_TEST(memory_fixed_array)
{
    FixedArray<int, 3> array;
    CG_EXPECT_TRUE(array.Empty());
    CG_EXPECT_TRUE(array.TryPushBack(1));
    CG_EXPECT_TRUE(array.TryPushBack(2));
    CG_EXPECT_TRUE(array.TryEmplaceBack(3) != nullptr);
    CG_EXPECT_TRUE(array.Full());
    CG_EXPECT_FALSE(array.TryPushBack(4));
    CG_EXPECT_EQ(array.Size(), usize{3});
    CG_EXPECT_EQ(array[2], 3);

    FixedArray<int, 3> copy(array);
    CG_EXPECT_EQ(copy.Size(), usize{3});
    CG_EXPECT_EQ(copy.Back(), 3);

    FixedArray<int, 3> moved(std::move(copy));
    CG_EXPECT_EQ(moved.Size(), usize{3});
    CG_EXPECT_TRUE(copy.Empty());

    moved.PopBack();
    CG_EXPECT_EQ(moved.Size(), usize{2});
    moved.Clear();
    CG_EXPECT_TRUE(moved.Empty());
}

CG_TEST(memory_string)
{
    HeapAllocator heap;
    String text(heap, "hello");
    CG_EXPECT_STREQ(text.CStr(), "hello");
    CG_EXPECT_EQ(text.Size(), usize{5});

    CG_EXPECT_TRUE(text.TryAppend(StringView(" world")));
    CG_EXPECT_STREQ(text.CStr(), "hello world");
    CG_EXPECT_TRUE(text.TryAppend('!'));
    CG_EXPECT_STREQ(text.CStr(), "hello world!");
    CG_EXPECT_EQ(text.View().Size(), usize{12});

    CG_EXPECT_TRUE(text.TryAssign("alias"));
    CG_EXPECT_TRUE(text.TryAppend(text.View()));
    CG_EXPECT_STREQ(text.CStr(), "aliasalias");

    CG_EXPECT_TRUE(text.TryReserve(64));
    CG_EXPECT_TRUE(text.Capacity() >= 64);
    text.Clear();
    CG_EXPECT_TRUE(text.Empty());
    CG_EXPECT_STREQ(text.CStr(), "");

    CG_EXPECT_TRUE(text.TryAssign("moved"));
    String moved(std::move(text));
    CG_EXPECT_STREQ(moved.CStr(), "moved");
    CG_EXPECT_TRUE(text.Empty());

    String assigned(heap);
    assigned = std::move(moved);
    CG_EXPECT_STREQ(assigned.CStr(), "moved");
    CG_EXPECT_TRUE(moved.Empty());
}

CG_TEST(memory_hash_map)
{
    HeapAllocator heap;
    HashMap<int, int> map(heap);
    CG_EXPECT_TRUE(map.Empty());

    for (int key = 0; key < 100; ++key)
    {
        bool inserted = false;
        CG_EXPECT_TRUE(map.TryInsert(key, key * 10, &inserted));
        CG_EXPECT_TRUE(inserted);
    }
    CG_EXPECT_EQ(map.Size(), usize{100});
    CG_EXPECT_TRUE(map.Capacity() >= 128);

    const int* found = map.Find(50);
    CG_REQUIRE(found != nullptr);
    CG_EXPECT_EQ(*found, 500);
    CG_EXPECT_TRUE(map.Contains(99));
    CG_EXPECT_TRUE(map.Find(1000) == nullptr);

    bool inserted = true;
    CG_EXPECT_TRUE(map.TryInsert(50, 7, &inserted));
    CG_EXPECT_FALSE(inserted);
    CG_EXPECT_EQ(*map.Find(50), 7);

    for (int key = 0; key < 50; ++key)
    {
        CG_EXPECT_TRUE(map.Remove(key));
    }
    CG_EXPECT_EQ(map.Size(), usize{50});
    CG_EXPECT_TRUE(map.Find(0) == nullptr);
    CG_EXPECT_TRUE(map.Contains(50));

    for (int key = 100; key < 200; ++key)
    {
        CG_EXPECT_TRUE(map.TryInsert(key, key * 10));
    }
    CG_EXPECT_EQ(map.Size(), usize{150});

    usize visited = 0;
    map.ForEach(
        [&visited](const int&, int& value)
        {
            ++visited;
            value += 1;
        });
    CG_EXPECT_EQ(visited, usize{150});

    CG_EXPECT_TRUE(map.TryReserve(1000));
    CG_EXPECT_TRUE(map.Capacity() >= 1000);

    map.Clear();
    CG_EXPECT_TRUE(map.Empty());
    CG_EXPECT_TRUE(map.Find(50) == nullptr);

    HashMap<int, int> moved(std::move(map));
    CG_EXPECT_TRUE(moved.Empty());
    CG_EXPECT_TRUE(map.TryInsert(1, 1));
    CG_EXPECT_EQ(map.Size(), usize{1});
}

CG_TEST(memory_hash_map_string_keys)
{
    HeapAllocator heap;
    HashMap<StringView, int> map(heap);

    CG_EXPECT_TRUE(map.TryInsert(StringView("alpha"), 1));
    CG_EXPECT_TRUE(map.TryInsert(StringView("beta"), 2));
    CG_EXPECT_EQ(*map.Find("alpha"), 1);
    CG_EXPECT_EQ(*map.Find("beta"), 2);
    CG_EXPECT_TRUE(map.Find("gamma") == nullptr);
    CG_EXPECT_TRUE(map.Remove("alpha"));
    CG_EXPECT_FALSE(map.Contains("alpha"));
}

CG_TEST(memory_slot_map)
{
    HeapAllocator heap;
    SlotMap<int> slots(heap);

    const Handle<int> first = slots.Insert(10);
    const Handle<int> second = slots.Insert(20);
    CG_REQUIRE(first.IsValid());
    CG_REQUIRE(second.IsValid());
    CG_EXPECT_EQ(first.index, u32{0});
    CG_EXPECT_EQ(second.index, u32{1});
    CG_EXPECT_EQ(*slots.Get(first), 10);
    CG_EXPECT_EQ(*slots.Get(second), 20);

    CG_EXPECT_TRUE(slots.Remove(first));
    CG_EXPECT_TRUE(slots.Get(first) == nullptr);
    CG_EXPECT_FALSE(slots.Contains(first));

    const Handle<int> third = slots.Insert(30);
    CG_REQUIRE(third.IsValid());
    CG_EXPECT_EQ(third.index, first.index);
    CG_EXPECT_TRUE(third.generation != first.generation);
    CG_EXPECT_TRUE(slots.Get(first) == nullptr);
    CG_EXPECT_EQ(*slots.Get(third), 30);

    CG_EXPECT_TRUE(slots.Remove(second));
    slots.Clear();
    CG_EXPECT_TRUE(slots.Empty());
    CG_EXPECT_TRUE(slots.Get(third) == nullptr);

    const Handle<int> fourth = slots.Insert(40);
    CG_REQUIRE(fourth.IsValid());
    CG_EXPECT_TRUE(slots.Get(third) == nullptr);
    CG_EXPECT_EQ(*slots.Get(fourth), 40);

    CG_EXPECT_FALSE(slots.Remove(Handle<int>{}));
    CG_EXPECT_TRUE(slots.TryReserve(128));
    CG_EXPECT_TRUE(slots.Capacity() >= 128);
}

namespace
{

struct TestTag
{
};

}  // namespace

CG_TEST(memory_handle)
{
    Handle<TestTag> handle{3, 7};
    CG_EXPECT_TRUE(handle.IsValid());
    CG_EXPECT_EQ(handle.index, u32{3});
    CG_EXPECT_EQ(handle.generation, u32{7});

    Handle<TestTag> copy = handle;
    CG_EXPECT_TRUE(copy == handle);

    copy.Reset();
    CG_EXPECT_FALSE(copy.IsValid());
    CG_EXPECT_TRUE(copy != handle);

    HeapAllocator heap;
    SlotMap<int, TestTag> tagged(heap);
    const Handle<TestTag> taggedHandle = tagged.Insert(99);
    CG_REQUIRE(taggedHandle.IsValid());
    CG_EXPECT_EQ(*tagged.Get(taggedHandle), 99);
}
