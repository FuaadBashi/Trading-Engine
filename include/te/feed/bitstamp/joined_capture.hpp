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
    // Raw payload has a line but the frame index ended.
    frame_index_ended_early,

    // Frame index has a line but the raw payload ended.
    payload_ended_early,

    payload_unreadable,
    frame_index_unreadable,
    frame_malformed,

    // The frame is valid JSON but streamKind is unsupported.
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
};

struct JoinedCapture {
    BookSnapshot seed;
    BookSnapshot checkpoint;

    
    std::vector<OrderEvent> jc_orderEvents;
    std::vector<TradeEvent> jc_tradeEvents;
};

Result<JoinedCapture, JoinedCaptureError> loadJoinedCapture(
    const std::filesystem::path& captureDirectory, InstrumentSpec spec);

}  // namespace te::bitstamp
