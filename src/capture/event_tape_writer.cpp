#include "te/capture/event_tape_writer.hpp"

#include "te/feed/merge_cursor.hpp"
#include "te/telemetry/event_record_format.hpp"
#include "te/telemetry/event_segment_writer.hpp"

namespace te {

Result<TapeWriteStats, TapeWriteError> writeEventTape(const JoinedCapture& capture,
                                                      const SegmentHeader& header,
                                                      std::uint64_t seedMicros,
                                                      std::uint64_t cutoffMicros,
                                                      const std::filesystem::path& outputPath) {
    // Checked here rather than relying on the writer's own rejection, so the caller gets an error
    // that names the actual mistake instead of a generic encode failure.
    if (header.recordSize != kEventRecordSize) {
        return Result<TapeWriteStats, TapeWriteError>::failure(
            TapeWriteError::header_not_event_format);
    }

    SegmentHeader tapeHeader = header;
    tapeHeader.orderingPolicyVersion = kOrderingPolicyOrderWinsTie;

    auto opened = EventSegmentWriter::open(outputPath, tapeHeader);
    if (!opened.hasValue()) {
        return Result<TapeWriteStats, TapeWriteError>::failure(TapeWriteError::output_open_failed);
    }
    EventSegmentWriter& writer = *opened.valueIf();

    MergeCursor cursor{capture.jc_captureOrderEvents, capture.jc_tradeEvents, seedMicros,
                       cutoffMicros};

    TapeWriteStats stats{};
    stats.ordersBeforeSeed = cursor.ordersBeforeSeed();
    stats.tradesBeforeSeed = cursor.tradesBeforeSeed();

    while (const auto pick = cursor.next()) {
        if (pick->stream == MergedStream::order) {
            const CapturedOrderEvent& captured = capture.jc_captureOrderEvents.at(pick->index);
            const auto appended = writer.append(
                DecodedEventRecord{DecodedOrderRecord{captured.event, captured.amountTraded}});
            if (!appended.hasValue()) {
                return Result<TapeWriteStats, TapeWriteError>::failure(
                    TapeWriteError::record_append_failed);
            }
            ++stats.ordersWritten;
        } else {
            const CapturedTradeEvent& captured = capture.jc_tradeEvents.at(pick->index);
            const auto appended = writer.append(DecodedEventRecord{captured.event});
            if (!appended.hasValue()) {
                return Result<TapeWriteStats, TapeWriteError>::failure(
                    TapeWriteError::record_append_failed);
            }
            ++stats.tradesWritten;
        }
    }

    stats.ordersAfterCutoff = cursor.ordersAfterCutoff();
    stats.tradesAfterCutoff = cursor.tradesAfterCutoff();

    // finish() rather than letting the stream close itself, so a failed final flush is reported
    // instead of leaving a short file that looks complete.
    const auto finished = writer.finish();
    if (!finished.hasValue()) {
        return Result<TapeWriteStats, TapeWriteError>::failure(TapeWriteError::finish_failed);
    }
    stats.bytesWritten = finished.valueIf()->bytesWritten;

    return Result<TapeWriteStats, TapeWriteError>::success(stats);
}

}  // namespace te
