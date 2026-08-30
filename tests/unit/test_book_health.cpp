#include <gtest/gtest.h>

#include "te/book/book_health.hpp"

TEST(BookHealth, SuccessfulSynchronizationMakesBookUsable) {
    te::BookHealth health;

    EXPECT_EQ(health.getState(), te::BookHealthState::unseeded);
    EXPECT_EQ(health.getFailureReason(), te::FailureReason::none);
    EXPECT_EQ(health.getFailureCount(), 0U);
    EXPECT_FALSE(health.isUsable());

    EXPECT_TRUE(health.startSynchronization());
    EXPECT_EQ(health.getState(), te::BookHealthState::synchronizing);
    EXPECT_FALSE(health.isUsable());

    EXPECT_TRUE(health.synchronizationSucceeded());
    EXPECT_EQ(health.getState(), te::BookHealthState::valid);
    EXPECT_EQ(health.getFailureReason(), te::FailureReason::none);
    EXPECT_EQ(health.getFailureCount(), 0U);
    EXPECT_TRUE(health.isUsable());
}

TEST(BookHealth, ThirdConsecutiveSynchronizationFailureBecomesFatal) {
    te::BookHealth health;

    ASSERT_TRUE(health.startSynchronization());
    EXPECT_TRUE(health.synchronizationFailed(te::FailureReason::snapshot_failure));
    EXPECT_EQ(health.getState(), te::BookHealthState::corrupted);
    EXPECT_EQ(health.getFailureCount(), 1U);

    ASSERT_TRUE(health.startSynchronization());
    EXPECT_TRUE(health.synchronizationFailed(te::FailureReason::snapshot_failure));
    EXPECT_EQ(health.getState(), te::BookHealthState::corrupted);
    EXPECT_EQ(health.getFailureCount(), 2U);

    ASSERT_TRUE(health.startSynchronization());
    EXPECT_TRUE(health.synchronizationFailed(te::FailureReason::snapshot_failure));
    EXPECT_EQ(health.getState(), te::BookHealthState::fatal_failure);
    EXPECT_EQ(health.getFailureReason(), te::FailureReason::snapshot_failure);
    EXPECT_EQ(health.getFailureCount(), 3U);
    EXPECT_FALSE(health.isUsable());
    EXPECT_FALSE(health.startSynchronization());
}

TEST(BookHealth, InvalidTransitionsLeaveStateUnchanged) {
    te::BookHealth health;

    EXPECT_FALSE(health.synchronizationSucceeded());
    EXPECT_FALSE(health.synchronizationFailed(te::FailureReason::snapshot_failure));
    EXPECT_FALSE(health.markCorrupted(te::FailureReason::event_gap));
    EXPECT_EQ(health.getState(), te::BookHealthState::unseeded);
    EXPECT_EQ(health.getFailureReason(), te::FailureReason::none);
    EXPECT_EQ(health.getFailureCount(), 0U);

    ASSERT_TRUE(health.startSynchronization());
    ASSERT_TRUE(health.synchronizationSucceeded());
    EXPECT_FALSE(health.markCorrupted(te::FailureReason::none));
    EXPECT_EQ(health.getState(), te::BookHealthState::valid);
    EXPECT_TRUE(health.isUsable());
}

TEST(BookHealth, SuccessfulResynchronizationClearsPreviousFailures) {
    te::BookHealth health;

    ASSERT_TRUE(health.startSynchronization());
    ASSERT_TRUE(health.synchronizationSucceeded());
    ASSERT_TRUE(health.markCorrupted(te::FailureReason::event_gap));
    EXPECT_FALSE(health.isUsable());

    ASSERT_TRUE(health.startSynchronization());
    ASSERT_TRUE(health.synchronizationFailed(te::FailureReason::snapshot_failure));
    EXPECT_EQ(health.getFailureCount(), 1U);

    ASSERT_TRUE(health.startSynchronization());
    ASSERT_TRUE(health.synchronizationSucceeded());
    EXPECT_EQ(health.getState(), te::BookHealthState::valid);
    EXPECT_EQ(health.getFailureReason(), te::FailureReason::none);
    EXPECT_EQ(health.getFailureCount(), 0U);
    EXPECT_TRUE(health.isUsable());
}
