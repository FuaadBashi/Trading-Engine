#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "te/capture/capture_validator.hpp"

namespace {

constexpr std::string_view kPayloadHash =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
constexpr std::string_view kFrameIndexHash =
    "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210";

// A JoinedCapture and a SegmentDescription that agree on every declared/actual pair:
// 2 order events, 1 trade event, 1 control frame -> 4 total frames. Each test starts from this
// matching baseline and changes exactly one field, so a failure can only be attributed to the
// one check that produced it.
te::SegmentDescription matchingDeclared() {
    te::SegmentDescription declared;
    declared.declaredPayloadBytes = 100;
    declared.declaredPayloadSha256 = std::string{kPayloadHash};
    declared.declaredFrameIndexBytes = 50;
    declared.declaredFrameIndexSha256 = std::string{kFrameIndexHash};
    declared.declaredFrameCount = 4;
    declared.declaredOrderEventCount = 2;
    declared.declaredTradeEventCount = 1;
    declared.declaredControlFrameCount = 1;
    declared.declaredChainValid = true;
    return declared;
}

te::JoinedCapture matchingCapture() {
    te::JoinedCapture capture;
    capture.actualPayloadBytes = 100;
    capture.actualPayloadSha256 = std::string{kPayloadHash};
    capture.actualFrameIndexBytes = 50;
    capture.actualFrameIndexSha256 = std::string{kFrameIndexHash};
    capture.actualControlFrameCount = 1;
    capture.jc_captureOrderEvents = std::vector<te::CapturedOrderEvent>(2);
    capture.jc_tradeEvents = std::vector<te::CapturedTradeEvent>(1);
    return capture;
}

}  // namespace

TEST(CaptureValidator, AcceptsACaptureThatMatchesItsManifestExactly) {
    const auto result = te::validateCapture(matchingCapture(), matchingDeclared());

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->capture().jc_captureOrderEvents.size(), 2U);
    EXPECT_EQ(result.valueIf()->capture().jc_tradeEvents.size(), 1U);
}

TEST(CaptureValidator, RejectsPayloadByteCountMismatch) {
    auto capture = matchingCapture();
    capture.actualPayloadBytes = 99;

    const auto result = te::validateCapture(capture, matchingDeclared());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ValidationError::payload_size_mismatch);
}

TEST(CaptureValidator, RejectsPayloadHashMismatchEvenWhenSizeMatches) {
    auto capture = matchingCapture();
    capture.actualPayloadSha256 =
        "1111111111111111111111111111111111111111111111111111111111111111";

    const auto result = te::validateCapture(capture, matchingDeclared());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ValidationError::payload_hash_mismatch)
        << "size is unchanged; a byte-for-byte content difference must be its own reason, not "
           "reported as a size mismatch";
}

TEST(CaptureValidator, RejectsFrameIndexByteCountMismatch) {
    auto capture = matchingCapture();
    capture.actualFrameIndexBytes = 49;

    const auto result = te::validateCapture(capture, matchingDeclared());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ValidationError::frame_index_size_mismatch);
}

TEST(CaptureValidator, RejectsFrameIndexHashMismatchEvenWhenSizeMatches) {
    auto capture = matchingCapture();
    capture.actualFrameIndexSha256 =
        "2222222222222222222222222222222222222222222222222222222222222222";

    const auto result = te::validateCapture(capture, matchingDeclared());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ValidationError::frame_index_hash_mismatch);
}

TEST(CaptureValidator, RejectsControlFrameCountMismatch) {
    auto capture = matchingCapture();
    capture.actualControlFrameCount = 0;

    const auto result = te::validateCapture(capture, matchingDeclared());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ValidationError::control_frame_count_mismatch);
}

TEST(CaptureValidator, RejectsOrderEventCountMismatch) {
    auto capture = matchingCapture();
    capture.jc_captureOrderEvents.pop_back();  // 2 declared, only 1 actually present

    const auto result = te::validateCapture(capture, matchingDeclared());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ValidationError::order_event_count_mismatch);
}

TEST(CaptureValidator, RejectsTradeEventCountMismatch) {
    auto capture = matchingCapture();
    capture.jc_tradeEvents.clear();  // 1 declared, 0 actually present

    const auto result = te::validateCapture(capture, matchingDeclared());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ValidationError::trade_event_count_mismatch);
}

TEST(CaptureValidator, RejectsFrameCountMismatchEvenWhenEverySubCountMatches) {
    // The manifest's own numbers disagree with themselves: 2 orders + 1 trade + 1 control is 4,
    // not the 5 declared here. Every individual actual count still matches its own declared
    // value, so only the total check can catch this -- proving frame_count_mismatch earns its
    // place rather than being redundant with the three sub-count checks.
    auto declared = matchingDeclared();
    declared.declaredFrameCount = 5;

    const auto result = te::validateCapture(matchingCapture(), declared);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ValidationError::frame_count_mismatch);
}
