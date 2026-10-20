#include <cyber/core/assert.h>
#include <cyber/core/build_config.h>
#include <cyber/core/error.h>
#include <cyber/core/hash.h>
#include <cyber/core/log.h>
#include <cyber/core/log_sinks.h>
#include <cyber/core/result.h>
#include <cyber/core/types.h>
#include <cyber/core/version.h>
#include <cyber/test/test.h>

#include <type_traits>

CG_TEST(public_headers_compile_together)
{
    static_assert(sizeof(cyber::core::u8) == 1);
    static_assert(sizeof(cyber::core::u16) == 2);
    static_assert(sizeof(cyber::core::u32) == 4);
    static_assert(sizeof(cyber::core::u64) == 8);
    static_assert(sizeof(cyber::core::f32) == 4);
    static_assert(sizeof(cyber::core::f64) == 8);
    static_assert(std::is_same_v<cyber::core::usize, std::size_t>);
    static_assert(std::is_same_v<cyber::core::isize, std::ptrdiff_t>);

    CG_EXPECT_EQ(cyber::core::kInvalidIndex, static_cast<cyber::core::usize>(-1));
}
