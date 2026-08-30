#pragma once

#include <filesystem>
#include <vector>
#include "te/core/instrument.hpp"
#include "te/core/result.hpp"
#include "te/feed/bitstamp/replay.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace te {

enum class CaptureCoordinatorError {
    manifest_failure,
    manifest_spec_mismatch,
    segment_load_failure,
    replay_failure
};

struct CheckpointComparison {
    bool matched{};
    std::size_t expectedLevelCount{};
    std::size_t actualLevelCount{};
    std::size_t mismatchedExpectedLevels{};
    std::size_t unexpectedActualLevels{};
    std::size_t expectedLevelsPresent{};
};

struct SegmentReplayReport {
    std::size_t segmentIndex{};
    std::uint64_t cutoffMicros{};
    bitstamp::ReplayStats replayStats;
    std::optional<CheckpointComparison> checkpointComparison;
};

struct CaptureReplayReport {
    std::vector<SegmentReplayReport> segments;
};

Result<CaptureReplayReport, CaptureCoordinatorError> captureCoordinator(const std::filesystem::path& captureDirectory, InstrumentSpec spec);

}
