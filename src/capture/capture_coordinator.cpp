
#include "te/capture/capture_coordinator.hpp"
#include "te/capture/capture_validator.hpp"
#include "te/capture/manifest_reader.hpp"
#include "te/capture/segment_loader.hpp"
#include <te/feed/bitstamp/replay.hpp>
#include <algorithm>
#include <limits>
#include <unordered_map>

namespace te {

Result<CaptureReplayReport, CaptureCoordinatorError> captureCoordinator(const std::filesystem::path& captureDirectory, InstrumentSpec spec)
{
    bitstamp::Replay replay;
    CaptureReplayReport captureReplayReport;

    const auto manifestResult = manifestReader(captureDirectory);
    if (!manifestResult.hasValue()) {
        return Result<CaptureReplayReport, CaptureCoordinatorError>::failure(
            CaptureCoordinatorError::manifest_failure);
    }

    const CaptureManifest& captureManifest = *manifestResult.valueIf();
    if (captureManifest.venue != spec.venue_id ||
        captureManifest.instrument != spec.instrument_id) {
        return Result<CaptureReplayReport, CaptureCoordinatorError>::failure(
            CaptureCoordinatorError::manifest_spec_mismatch);
    }

    for (const SegmentDescription& segment : captureManifest.segments){
        SegmentReplayReport segmentReport;

        Result<JoinedCapture, JoinedCaptureError> loadResult = loadSegment(segment, spec);
        if (!loadResult.hasValue()) {
            return Result<CaptureReplayReport, CaptureCoordinatorError>::failure(
                CaptureCoordinatorError::segment_load_failure);
        }

        const Result<ValidatedCapture, ValidationError> validated =
            validateCapture(std::move(*loadResult.valueIf()), segment);
        if (!validated.hasValue()) {
            return Result<CaptureReplayReport, CaptureCoordinatorError>::failure(
                CaptureCoordinatorError::capture_validation_failure);
        }

        const JoinedCapture& capture = validated.valueIf()->capture();

        std::uint64_t cutoff = capture.seed.microtimestamp;
        if (capture.checkpoint.has_value()) {
            cutoff = capture.checkpoint->microtimestamp;
        } else {
            if (!capture.jc_captureOrderEvents.empty()) {
                cutoff = std::max(
                    cutoff,
                    capture.jc_captureOrderEvents.back().event.venue_timestamp_us);
            }
            if (!capture.jc_tradeEvents.empty()) {
                cutoff = std::max(
                    cutoff,
                    capture.jc_tradeEvents.back().event.venue_timestamp_us);
            }
        }

        Result<bitstamp::ReplayResult, bitstamp::ReplayError> replayResults = replay.replay(
            capture.seed,
            capture.jc_captureOrderEvents,
            capture.jc_tradeEvents,
            cutoff
        );
        if (!replayResults.hasValue()) {
            return Result<CaptureReplayReport, CaptureCoordinatorError>::failure(
                CaptureCoordinatorError::replay_failure);
        }
        const bitstamp::ReplayResult& replayed = *replayResults.valueIf();
        const OrderBook& replayedBook = replayed.book;

        if (capture.checkpoint.has_value()) {
            CheckpointComparison checkpointComparison;
            std::unordered_map<Price, Qty, PriceHash> expectedBids;
            std::unordered_map<Price, Qty, PriceHash> expectedAsks;

            for (const bitstamp::SnapshotOrder& order : capture.checkpoint->orders) {
                if (order.side == Side::buy) {
                    expectedBids[order.price].units += order.quantity.units;
                } else if (order.side == Side::sell) {
                    expectedAsks[order.price].units += order.quantity.units;
                }
            }
            // Same check-before-adding shape as price_level.cpp's addOrder. A real book can never
            // approach SIZE_MAX price levels, but consistency with the project's own standard.
            checkpointComparison.expectedLevelCount =
                (expectedBids.size() > std::numeric_limits<std::size_t>::max() - expectedAsks.size())
                    ? std::numeric_limits<std::size_t>::max()
                    : expectedBids.size() + expectedAsks.size();
            checkpointComparison.actualLevelCount = replayedBook.levelCount();

            for (const auto& [price, expectedQuantity] : expectedBids) {
                const Qty actualQuantity = replayedBook.qtyAt(Side::buy, price);

                if (actualQuantity.units != 0) {
                    ++checkpointComparison.expectedLevelsPresent;
                }
                if (actualQuantity != expectedQuantity) {
                    ++checkpointComparison.mismatchedExpectedLevels;
                }
            }
            for (const auto& [price, expectedQuantity] : expectedAsks) {
                const Qty actualQuantity = replayedBook.qtyAt(Side::sell, price);

                if (actualQuantity.units != 0) {
                    ++checkpointComparison.expectedLevelsPresent;
                }
                if (actualQuantity != expectedQuantity) {
                    ++checkpointComparison.mismatchedExpectedLevels;
                }
            }

            checkpointComparison.unexpectedActualLevels =
                checkpointComparison.actualLevelCount -
                checkpointComparison.expectedLevelsPresent;
            checkpointComparison.matched =
                checkpointComparison.mismatchedExpectedLevels == 0 &&
                checkpointComparison.unexpectedActualLevels == 0;

            segmentReport.checkpointComparison = checkpointComparison;
        }

        segmentReport.cutoffMicros = cutoff;
        segmentReport.replayStats = replayed.stats;
        segmentReport.segmentIndex = segment.index;
        segmentReport.finalBookDigest = replayed.book.digest();
        captureReplayReport.segments.push_back(segmentReport);
    }

    return Result<CaptureReplayReport, CaptureCoordinatorError>::success(captureReplayReport);
}

}
