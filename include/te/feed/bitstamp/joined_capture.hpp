#pragma once

#include <filesystem>
#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/feed/bitstamp/snapshot.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>
#include <vector>

namespace te::bitstamp {

enum class JoinedCaptureError {
    frame_index_ended_early,
    payload_ended_early,

    payload_unreadable,
    frame_index_unreadable,
    frame_malformed,

    unknown_stream_kind,

    manifest_unreadable,
    manifest_malformed,
    manifest_missing_field,
    manifest_missing_segment,
    manifest_invalid_structure,

    seed_unreadable,
    seed_parse_failure,
    checkpoint_unreadable,
    checkpoint_parse_failure,
    order_decode_failure,
    trade_decode_failure,
    fill_decode_failure
};
struct CapturedOrderEvent {
    OrderEvent  event;
    Qty  amountTraded;
};

struct JoinedCapture {
    // S0 seeds replay; the later independent S1 checkpoint is its correctness oracle.
    BookSnapshot seed;
    BookSnapshot checkpoint;

    // Payload/frame rows are joined and decoded; control frames are deliberately omitted.
    std::vector<CapturedOrderEvent> jc_captureOrderEvents;
    std::vector<TradeEvent> jc_tradeEvents;
};


// Loads the first manifest segment and requires payload/frame-index files to end together.
// It decodes files but does not verify hashes or chains; validate_joined_capture.py does that.
// Current limitation: captureOrdinal is not preserved yet.
Result<JoinedCapture, JoinedCaptureError> loadJoinedCapture(
    const std::filesystem::path& captureDirectory, InstrumentSpec specs);

}  // namespace te::bitstamp
