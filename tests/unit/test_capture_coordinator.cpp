#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "te/capture/capture_coordinator.hpp"

namespace {

class TempCaptureDirectory {
public:
    explicit TempCaptureDirectory(std::string_view name)
        : path_{std::filesystem::temp_directory_path() / name} {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempCaptureDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempCaptureDirectory(const TempCaptureDirectory&) = delete;
    TempCaptureDirectory& operator=(const TempCaptureDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

te::InstrumentSpec btcUsd() {
    return te::InstrumentSpec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
}

void writeTextFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output{path, std::ios::binary};
    ASSERT_TRUE(output.is_open());
    output << contents;
    ASSERT_TRUE(output.good());
}

void writeEmptyStreams(const TempCaptureDirectory& capture, std::string_view prefix) {
    writeTextFile(capture.path() / (std::string{prefix} + ".jsonl"), "");
    writeTextFile(capture.path() / (std::string{prefix} + ".frames.jsonl"), "");
}

TEST(CaptureCoordinator, ReportsMatchingCheckpoint) {
    const TempCaptureDirectory capture{"te_capture_coordinator_match"};
    writeTextFile(capture.path() / "manifest.json", R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot",
        "checkpoint": "checkpoint-0000.snapshot",
        "payload_bytes": 0,
        "payload_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames_bytes": 0,
        "frames_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames": 0,
        "order_events": 0,
        "trade_events": 0,
        "control_frames": 0,
        "chain_valid": true
      }]
    })");
    writeTextFile(capture.path() / "segment-0000.snapshot",
                  R"({"microtimestamp":"1000","bids":[["100.00","2.00000000","42"]],"asks":[]})");
    writeTextFile(capture.path() / "checkpoint-0000.snapshot",
                  R"({"microtimestamp":"2000","bids":[["100.00","2.00000000","42"]],"asks":[]})");
    writeEmptyStreams(capture, "segment-0000");

    const auto result = te::captureCoordinator(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.valueIf()->segments.size(), 1U);
    const te::SegmentReplayReport& report = result.valueIf()->segments.front();
    ASSERT_TRUE(report.checkpointComparison.has_value());
    EXPECT_TRUE(report.checkpointComparison->matched);
    EXPECT_EQ(report.checkpointComparison->expectedLevelCount, 1U);
    EXPECT_EQ(report.checkpointComparison->actualLevelCount, 1U);
    EXPECT_EQ(report.checkpointComparison->mismatchedExpectedLevels, 0U);
    EXPECT_EQ(report.checkpointComparison->unexpectedActualLevels, 0U);
}

TEST(CaptureCoordinator, ReportsWrongQuantityAsMismatch) {
    const TempCaptureDirectory capture{"te_capture_coordinator_quantity_mismatch"};
    writeTextFile(capture.path() / "manifest.json", R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot",
        "checkpoint": "checkpoint-0000.snapshot",
        "payload_bytes": 0,
        "payload_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames_bytes": 0,
        "frames_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames": 0,
        "order_events": 0,
        "trade_events": 0,
        "control_frames": 0,
        "chain_valid": true
      }]
    })");
    writeTextFile(capture.path() / "segment-0000.snapshot",
                  R"({"microtimestamp":"1000","bids":[["100.00","2.00000000","42"]],"asks":[]})");
    writeTextFile(capture.path() / "checkpoint-0000.snapshot",
                  R"({"microtimestamp":"2000","bids":[["100.00","1.00000000","42"]],"asks":[]})");
    writeEmptyStreams(capture, "segment-0000");

    const auto result = te::captureCoordinator(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_TRUE(result.valueIf()->segments.front().checkpointComparison.has_value());
    const te::CheckpointComparison& comparison =
        *result.valueIf()->segments.front().checkpointComparison;
    EXPECT_FALSE(comparison.matched);
    EXPECT_EQ(comparison.mismatchedExpectedLevels, 1U);
    EXPECT_EQ(comparison.unexpectedActualLevels, 0U);
}

TEST(CaptureCoordinator, ReportsUnexpectedActualLevel) {
    const TempCaptureDirectory capture{"te_capture_coordinator_unexpected_level"};
    writeTextFile(capture.path() / "manifest.json", R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot",
        "checkpoint": "checkpoint-0000.snapshot",
        "payload_bytes": 0,
        "payload_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames_bytes": 0,
        "frames_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames": 0,
        "order_events": 0,
        "trade_events": 0,
        "control_frames": 0,
        "chain_valid": true
      }]
    })");
    writeTextFile(capture.path() / "segment-0000.snapshot",
                  R"({"microtimestamp":"1000","bids":[["99.00","1.00000000","7"]],"asks":[]})");
    writeTextFile(capture.path() / "checkpoint-0000.snapshot",
                  R"({"microtimestamp":"2000","bids":[],"asks":[]})");
    writeEmptyStreams(capture, "segment-0000");

    const auto result = te::captureCoordinator(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    const te::CheckpointComparison& comparison =
        *result.valueIf()->segments.front().checkpointComparison;
    EXPECT_FALSE(comparison.matched);
    EXPECT_EQ(comparison.mismatchedExpectedLevels, 0U);
    EXPECT_EQ(comparison.unexpectedActualLevels, 1U);
}

TEST(CaptureCoordinator, LeavesCheckpointComparisonEmptyWhenCheckpointIsAbsent) {
    const TempCaptureDirectory capture{"te_capture_coordinator_no_checkpoint"};
    writeTextFile(capture.path() / "manifest.json", R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot",
        "payload_bytes": 0,
        "payload_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames_bytes": 0,
        "frames_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames": 0,
        "order_events": 0,
        "trade_events": 0,
        "control_frames": 0,
        "chain_valid": true
      }]
    })");
    writeTextFile(capture.path() / "segment-0000.snapshot",
                  R"({"microtimestamp":"1000","bids":[],"asks":[]})");
    writeEmptyStreams(capture, "segment-0000");

    const auto result = te::captureCoordinator(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.valueIf()->segments.size(), 1U);
    EXPECT_EQ(result.valueIf()->segments.front().cutoffMicros, 1000U);
    EXPECT_FALSE(result.valueIf()->segments.front().checkpointComparison.has_value());
}

TEST(CaptureCoordinator, ProcessesEverySegmentIndependently) {
    const TempCaptureDirectory capture{"te_capture_coordinator_multiple_segments"};
    writeTextFile(capture.path() / "manifest.json", R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [
        {
          "index": 0,
          "payload": "segment-0000.jsonl",
          "frame_index": "segment-0000.frames.jsonl",
          "snapshot": "segment-0000.snapshot",
          "payload_bytes": 0,
          "payload_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "frames_bytes": 0,
          "frames_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "frames": 0,
          "order_events": 0,
          "trade_events": 0,
          "control_frames": 0,
          "chain_valid": true
        },
        {
          "index": 1,
          "payload": "segment-0001.jsonl",
          "frame_index": "segment-0001.frames.jsonl",
          "snapshot": "segment-0001.snapshot",
          "payload_bytes": 0,
          "payload_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "frames_bytes": 0,
          "frames_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "frames": 0,
          "order_events": 0,
          "trade_events": 0,
          "control_frames": 0,
          "chain_valid": true
        }
      ]
    })");
    writeTextFile(capture.path() / "segment-0000.snapshot",
                  R"({"microtimestamp":"1000","bids":[],"asks":[]})");
    writeTextFile(capture.path() / "segment-0001.snapshot",
                  R"({"microtimestamp":"5000","bids":[],"asks":[]})");
    writeEmptyStreams(capture, "segment-0000");
    writeEmptyStreams(capture, "segment-0001");

    const auto result = te::captureCoordinator(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.valueIf()->segments.size(), 2U);
    EXPECT_EQ(result.valueIf()->segments.at(0).segmentIndex, 0U);
    EXPECT_EQ(result.valueIf()->segments.at(0).cutoffMicros, 1000U);
    EXPECT_EQ(result.valueIf()->segments.at(1).segmentIndex, 1U);
    EXPECT_EQ(result.valueIf()->segments.at(1).cutoffMicros, 5000U);
}

TEST(CaptureCoordinator, RejectsManifestAndSpecMismatch) {
    const TempCaptureDirectory capture{"te_capture_coordinator_spec_mismatch"};
    writeTextFile(capture.path() / "manifest.json", R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "missing.jsonl",
        "frame_index": "missing.frames.jsonl",
        "snapshot": "missing.snapshot",
        "payload_bytes": 100,
        "payload_sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        "frames_bytes": 50,
        "frames_sha256": "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210",
        "frames": 4,
        "order_events": 2,
        "trade_events": 1,
        "control_frames": 1,
        "chain_valid": true
      }]
    })");
    te::InstrumentSpec wrongSpec = btcUsd();
    wrongSpec.instrument_id = te::InstrumentId::btc_gbp;

    const auto result = te::captureCoordinator(capture.path(), wrongSpec);

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::CaptureCoordinatorError::manifest_spec_mismatch);
}

TEST(CaptureCoordinator, RejectsCaptureWhoseDeclaredByteCountExceedsWhatWasActuallyWritten) {
    const TempCaptureDirectory capture{"te_capture_coordinator_truncated_payload"};
    writeTextFile(capture.path() / "manifest.json", R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot",
        "payload_bytes": 5,
        "payload_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames_bytes": 0,
        "frames_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames": 0,
        "order_events": 0,
        "trade_events": 0,
        "control_frames": 0,
        "chain_valid": true
      }]
    })");
    writeTextFile(capture.path() / "segment-0000.snapshot",
                  R"({"microtimestamp":"1000","bids":[],"asks":[]})");
    // Manifest declares 5 payload bytes; the payload file actually written is empty -- the
    // truncated-capture scenario the loader-completeness check exists to catch. loadSegment()
    // still succeeds (0 payload lines, 0 frame-index lines is internally consistent), so this
    // exercises validateCapture()'s rejection specifically, not a load failure.
    writeEmptyStreams(capture, "segment-0000");

    const auto result = te::captureCoordinator(capture.path(), btcUsd());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::CaptureCoordinatorError::capture_validation_failure);
}

TEST(CaptureCoordinator, RejectsCheckpointWhoseAggregatedQuantityWouldOverflow) {
    const TempCaptureDirectory capture{"te_capture_coordinator_checkpoint_overflow"};
    writeTextFile(capture.path() / "manifest.json", R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot",
        "checkpoint": "checkpoint-0000.snapshot",
        "payload_bytes": 0,
        "payload_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames_bytes": 0,
        "frames_sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "frames": 0,
        "order_events": 0,
        "trade_events": 0,
        "control_frames": 0,
        "chain_valid": true
      }]
    })");
    writeTextFile(capture.path() / "segment-0000.snapshot",
                  R"({"microtimestamp":"1000","bids":[],"asks":[]})");
    // Two orders at the SAME price, each just over half of INT64_MAX units (8 decimal places, so
    // 92233720368.0 -> 9223372036800000000 units). Individually valid; aggregating them onto one
    // level overflows. Unguarded this wraps negative and the comparison silently reports a
    // nonsense expected quantity instead of refusing the checkpoint.
    writeTextFile(capture.path() / "checkpoint-0000.snapshot",
                  R"({"microtimestamp":"2000","bids":[["100.00","92233720368.00000000","42"],)"
                  R"(["100.00","92233720368.00000000","43"]],"asks":[]})");
    writeEmptyStreams(capture, "segment-0000");

    const auto result = te::captureCoordinator(capture.path(), btcUsd());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::CaptureCoordinatorError::checkpoint_quantity_overflow);
}

TEST(CaptureCoordinator, GoldenCaptureMatchesIndependentCheckpoint) {
    const std::filesystem::path capture =
        std::filesystem::path{TE_TEST_DATA_DIR} / "joined-capture-golden";
    ASSERT_TRUE(std::filesystem::exists(capture / "manifest.json"));

    const auto result = te::captureCoordinator(capture, btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    ASSERT_EQ(result.valueIf()->segments.size(), 1U);
    const te::SegmentReplayReport& report = result.valueIf()->segments.front();
    ASSERT_TRUE(report.checkpointComparison.has_value());
    EXPECT_TRUE(report.checkpointComparison->matched);
    EXPECT_EQ(report.checkpointComparison->mismatchedExpectedLevels, 0U);
    EXPECT_EQ(report.checkpointComparison->unexpectedActualLevels, 0U);
}

TEST(CaptureCoordinator, GoldenCaptureProducesIdenticalDigestsAcrossRepeatedRuns) {
    const std::filesystem::path capture =
        std::filesystem::path{TE_TEST_DATA_DIR} / "joined-capture-golden";

    const auto first = te::captureCoordinator(capture, btcUsd());
    const auto second = te::captureCoordinator(capture, btcUsd());

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(second.hasValue());
    ASSERT_EQ(first.valueIf()->segments.size(), 1U);
    ASSERT_EQ(second.valueIf()->segments.size(), 1U);
    const te::SegmentReplayReport& firstReport = first.valueIf()->segments.front();
    const te::SegmentReplayReport& secondReport = second.valueIf()->segments.front();
    EXPECT_EQ(firstReport.finalBookDigest, secondReport.finalBookDigest);
    EXPECT_EQ(firstReport.replayStats.appliedEventDigest,
              secondReport.replayStats.appliedEventDigest);
}

}  // namespace
