#include <cyber/core/error.h>
#include <cyber/core/result.h>
#include <cyber/test/test.h>

#include <string>
#include <type_traits>
#include <utility>

namespace
{

struct MoveOnly
{
    explicit MoveOnly(int initialValue) noexcept : value(initialValue) {}

    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&&) noexcept = default;
    MoveOnly& operator=(MoveOnly&&) noexcept = default;

    int value = 0;
};

struct Tracker
{
    Tracker() noexcept
    {
        ++alive;
    }

    Tracker(const Tracker&) = delete;
    Tracker& operator=(const Tracker&) = delete;

    Tracker(Tracker&& other) noexcept : value(other.value)
    {
        ++alive;
    }

    Tracker& operator=(Tracker&&) noexcept = default;

    ~Tracker()
    {
        --alive;
    }

    static int alive;
    int value = 0;
};

int Tracker::alive = 0;

}  // namespace

CG_TEST(result_ok_and_error_access)
{
    auto ok = cyber::core::Result<int, cyber::core::Error>::Ok(42);
    CG_EXPECT_TRUE(ok.IsOk());
    CG_EXPECT_FALSE(ok.IsError());
    CG_EXPECT_TRUE(static_cast<bool>(ok));
    CG_EXPECT_EQ(ok.Value(), 42);

    auto error = cyber::core::Result<int, cyber::core::Error>::Err(
        cyber::core::Error{cyber::core::ErrorCode::NotFound, "missing value"});
    CG_EXPECT_FALSE(error.IsOk());
    CG_EXPECT_TRUE(error.IsError());
    CG_EXPECT_FALSE(static_cast<bool>(error));
    CG_EXPECT_EQ(error.Error().code, cyber::core::ErrorCode::NotFound);
    CG_EXPECT_STREQ(error.Error().message, "missing value");
}

CG_TEST(result_void)
{
    auto ok = cyber::core::Result<void, cyber::core::Error>::Ok();
    CG_EXPECT_TRUE(ok.IsOk());
    CG_EXPECT_FALSE(ok.IsError());

    auto error = cyber::core::Status<cyber::core::Error>::Err(
        cyber::core::Error{cyber::core::ErrorCode::Unsupported, "not available"});
    CG_EXPECT_FALSE(error.IsOk());
    CG_EXPECT_TRUE(error.IsError());
    CG_EXPECT_EQ(error.Error().code, cyber::core::ErrorCode::Unsupported);
}

CG_TEST(result_move_only_value)
{
    auto result = cyber::core::Result<MoveOnly, cyber::core::Error>::Ok(MoveOnly(7));
    CG_EXPECT_EQ(result.Value().value, 7);

    auto moved = std::move(result);
    CG_EXPECT_TRUE(moved.IsOk());
    CG_EXPECT_EQ(moved.Value().value, 7);
}

CG_TEST(result_move_assignment)
{
    auto first = cyber::core::Result<int, cyber::core::Error>::Ok(1);
    auto second = cyber::core::Result<int, cyber::core::Error>::Err(
        cyber::core::Error{cyber::core::ErrorCode::Internal, "failure"});

    first = std::move(second);
    CG_EXPECT_TRUE(first.IsError());
    CG_EXPECT_EQ(first.Error().code, cyber::core::ErrorCode::Internal);
}

CG_TEST(result_conditional_copy)
{
    static_assert(std::is_copy_constructible_v<cyber::core::Result<int, cyber::core::Error>>);
    static_assert(std::is_copy_assignable_v<cyber::core::Result<int, cyber::core::Error>>);
    static_assert(
        !std::is_copy_constructible_v<cyber::core::Result<std::string, cyber::core::Error>>);
    static_assert(!std::is_copy_assignable_v<cyber::core::Result<std::string, cyber::core::Error>>);

    auto original = cyber::core::Result<int, cyber::core::Error>::Ok(5);
    auto copy = original;
    CG_EXPECT_TRUE(copy.IsOk());
    CG_EXPECT_EQ(copy.Value(), 5);

    auto error = cyber::core::Result<int, cyber::core::Error>::Err(
        cyber::core::Error{cyber::core::ErrorCode::OutOfRange, "range"});
    auto errorCopy = error;
    CG_EXPECT_TRUE(errorCopy.IsError());
    CG_EXPECT_EQ(errorCopy.Error().code, cyber::core::ErrorCode::OutOfRange);
}

CG_TEST(result_value_lifetime)
{
    CG_EXPECT_EQ(Tracker::alive, 0);
    {
        auto result = cyber::core::Result<Tracker, cyber::core::Error>::Ok(Tracker{});
        CG_EXPECT_EQ(Tracker::alive, 1);
        CG_EXPECT_EQ(result.Value().value, 0);

        auto moved = std::move(result);
        CG_EXPECT_EQ(Tracker::alive, 2);
        CG_EXPECT_EQ(moved.Value().value, 0);
    }
    CG_EXPECT_EQ(Tracker::alive, 0);
}

CG_TEST(error_code_names)
{
    using cyber::core::ErrorCode;
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::Ok), "ok");
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::InvalidArgument), "invalid_argument");
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::OutOfRange), "out_of_range");
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::NotFound), "not_found");
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::AlreadyExists), "already_exists");
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::Unsupported), "unsupported");
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::ParseError), "parse_error");
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::Overflow), "overflow");
    CG_EXPECT_STREQ(cyber::core::ErrorCodeName(ErrorCode::Internal), "internal");
    CG_EXPECT_TRUE(cyber::core::IsOk(ErrorCode::Ok));
    CG_EXPECT_FALSE(cyber::core::IsOk(ErrorCode::Internal));
}
