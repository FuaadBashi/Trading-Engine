#include <gtest/gtest.h>

#include <te/core/time.hpp>

namespace {

// 2024-01-01T00:00:00Z in nanoseconds since the Unix epoch. A sanity floor, not an exact
// expectation: a real clock reading "now" must be well past this regardless of when the test
// runs. Catches a clock that's broken and returning zero or a wildly wrong epoch.
constexpr te::Nanos kKnownPastNanos{1'704'067'200'000'000'000LL};

}  // namespace

TEST(Clock, SystemClockNowIsPlausible) {
    const te::Clock clock = te::makeSystemClock();

    const te::Nanos now = clock.now();

    EXPECT_GT(now, kKnownPastNanos);
}

TEST(Clock, SystemClockNowMovesForward) {
    const te::Clock clock = te::makeSystemClock();

    const te::Nanos first = clock.now();
    const te::Nanos second = clock.now();

    EXPECT_GE(second, first);
}

// steady_clock's epoch is unspecified -- there is no "plausible absolute value" to check, only
// that it never runs backward, which is the one guarantee a monotonic clock actually makes.
TEST(Clock, SteadyClockNeverRunsBackward) {
    const te::Clock clock = te::makeSystemClock();

    const te::Nanos first = clock.steadyNow();
    const te::Nanos second = clock.steadyNow();

    EXPECT_GE(second, first);
}

// Proves the actual reason Clock is a struct of callables instead of calling std::chrono
// directly: a caller can substitute a controlled clock and get a predictable, testable value.
TEST(Clock, FakeClockReturnsControlledValue) {
    te::Clock fake;
    fake.now = []() { return te::Nanos{42}; };
    fake.steadyNow = []() { return te::Nanos{7}; };

    EXPECT_EQ(fake.now(), te::Nanos{42});
    EXPECT_EQ(fake.steadyNow(), te::Nanos{7});
}
