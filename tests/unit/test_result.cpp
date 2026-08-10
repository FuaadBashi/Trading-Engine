#include <gtest/gtest.h>

#include <te/core/result.hpp>

namespace {

// Local test-only error type: Result must work for generic error types, not only ParseError.
enum class TestError {
    example_failure,
};

}  // namespace

// A success exposes only its value. ASSERT prevents an unsafe dereference if the contract breaks.
TEST(Result, SuccessContainsValueAndNoError) {
    const auto result = te::Result<int, TestError>::success(42);

    EXPECT_TRUE(result.has_value());

    const int* value = result.value_if();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(*value, 42);

    EXPECT_EQ(result.error_if(), nullptr);
}

// A failure exposes only its error. Both branches are tested because variant state is exclusive.
TEST(Result, FailureContainsErrorAndNoValue) {
    const auto result =
        te::Result<int, TestError>::failure(TestError::example_failure);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.value_if(), nullptr);

    const TestError* error = result.error_if();
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(*error, TestError::example_failure);
}
