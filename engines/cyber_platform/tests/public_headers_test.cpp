#include <cyber/platform/platform.h>
#include <cyber/platform/types.h>
#include <cyber/test/test.h>

#include <type_traits>

using namespace cyber::platform;

CG_TEST(platform_public_headers)
{
    static_assert(!std::is_copy_constructible_v<Window>);
    static_assert(std::is_move_constructible_v<Window>);
    static_assert(!std::is_copy_constructible_v<MemoryMappedFile>);
    static_assert(std::is_move_constructible_v<MemoryMappedFile>);
    static_assert(!std::is_copy_constructible_v<DynamicLibrary>);
    static_assert(std::is_move_constructible_v<DynamicLibrary>);
    static_assert(!std::is_copy_constructible_v<Thread>);
    static_assert(std::is_move_constructible_v<Thread>);

    WindowDesc desc{};
    CG_EXPECT_TRUE(desc.width > 0);
    CG_EXPECT_TRUE(desc.height > 0);

    Clock clock;
    CG_EXPECT_TRUE(clock.Seconds() >= 0.0);
    CG_EXPECT_EQ(IsWindowSystemSupported(), IsWindowSystemSupported());
}
