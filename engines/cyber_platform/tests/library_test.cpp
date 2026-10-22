#include <cyber/platform/library.h>
#include <cyber/test/test.h>

#include <utility>

using namespace cyber::platform;

namespace
{

const char* SystemLibrary()
{
#if defined(_WIN32)
    return "kernel32.dll";
#else
    return "libc.so.6";
#endif
}

const char* SystemSymbol()
{
#if defined(_WIN32)
    return "GetCurrentProcessId";
#else
    return "getpid";
#endif
}

}  // namespace

CG_TEST(platform_dynamic_library)
{
    auto opened = DynamicLibrary::Open(SystemLibrary());
    CG_REQUIRE(static_cast<bool>(opened));
    DynamicLibrary library = std::move(opened.Value());
    CG_EXPECT_TRUE(library.IsOpen());
    CG_EXPECT_TRUE(library.NativeHandle() != nullptr);
    CG_EXPECT_TRUE(library.GetSymbol(SystemSymbol()) != nullptr);
    CG_EXPECT_TRUE(library.GetSymbol(nullptr) == nullptr);

    using SimpleFunction = int (*)();
    auto function = library.GetFunction<SimpleFunction>(SystemSymbol());
    CG_EXPECT_TRUE(static_cast<bool>(function));
    auto missingFunction = library.GetFunction<SimpleFunction>("cyber_missing_symbol");
    CG_EXPECT_FALSE(static_cast<bool>(missingFunction));

    DynamicLibrary moved = std::move(library);
    CG_EXPECT_FALSE(library.IsOpen());
    CG_EXPECT_TRUE(moved.IsOpen());
    moved.Close();
    CG_EXPECT_FALSE(moved.IsOpen());
    moved.Close();

    auto missing = DynamicLibrary::Open("cyber_missing_library_12345");
    CG_EXPECT_FALSE(static_cast<bool>(missing));
    CG_EXPECT_FALSE(static_cast<bool>(DynamicLibrary::Open(nullptr)));
}
