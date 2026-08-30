
#include "te/feed/capture_coordinator.hpp"
#include "te/feed/segment_loader.hpp"
#include <te/feed/bitstamp/replay.hpp>
#include <algorithm>
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
        std::uint64_t cutoff{};
        SegmentReplayReport segmentReport;
        const auto loadResult = loadSegment(segment, spec);

        if (!loadResult.hasValue()) {
            return Result<CaptureReplayReport, CaptureCoordinatorError>::failure(
                CaptureCoordinatorError::segment_load_failure);
        }
        const JoinedCapture& joinedCapture = *loadResult.valueIf();
        cutoff = joinedCapture.seed.microtimestamp;
        if (joinedCapture.checkpoint.has_value()) {
            cutoff = joinedCapture.checkpoint->microtimestamp;
        } else {
            
          if (!joinedCapture.jc_captureOrderEvents.empty()) {
            cutoff = std::max(
                cutoff,
                joinedCapture.jc_captureOrderEvents.back()
                    .event.venue_timestamp_us);
        }

        if (!joinedCapture.jc_tradeEvents.empty()) {
            cutoff = std::max(
                cutoff,
                joinedCapture.jc_tradeEvents.back()
                    .event.venue_timestamp_us);
        }
        }

         Result<bitstamp::ReplayResult, bitstamp::ReplayError> replayResults = replay.replay (
            joinedCapture.seed, 
            joinedCapture.jc_captureOrderEvents, 
            joinedCapture.jc_tradeEvents,
            cutoff
        );
        if (!replayResults.hasValue()){
            return Result<CaptureReplayReport, CaptureCoordinatorError>::failure(
                CaptureCoordinatorError::replay_failure);
        }
        const bitstamp::ReplayResult& replayed = *replayResults.valueIf();
        const OrderBook& replayedBook = replayed.book;

        if (joinedCapture.checkpoint.has_value()){
            CheckpointComparison checkpointComparison;
            std::unordered_map<Price, Qty, PriceHash> expectedBids;
            std::unordered_map<Price, Qty, PriceHash> expectedAsks;


            for (const bitstamp::SnapshotOrder& order : joinedCapture.checkpoint->orders) {
                if (order.side == Side::buy){
                    expectedBids[order.price].units += order.quantity.units;

                } else if (order.side == Side::sell){
                    expectedAsks[order.price].units += order.quantity.units;
                }
            }
            checkpointComparison.expectedLevelCount = expectedBids.size() + expectedAsks.size();
            checkpointComparison.actualLevelCount = replayedBook.levelCount();

            for (const auto& [price, expectedQuantity] : expectedBids) {
                const Qty actualQuantity =
                    replayedBook.qtyAt(Side::buy, price);
           
                if (actualQuantity.units != 0) {
                    ++checkpointComparison.expectedLevelsPresent;
                }

                if (actualQuantity != expectedQuantity) {
                    ++checkpointComparison.mismatchedExpectedLevels;
                }
            }
            for (const auto& [price, expectedQuantity] : expectedAsks) {
                const Qty actualQuantity =
                    replayedBook.qtyAt(Side::sell, price);

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
        captureReplayReport.segments.push_back(segmentReport);
        
    };



    return Result<CaptureReplayReport, CaptureCoordinatorError>::success(captureReplayReport);
}

}
