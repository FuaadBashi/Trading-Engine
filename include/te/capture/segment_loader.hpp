#pragma once

#include <optional>
#include <vector>

#include "te/capture/manifest_reader.hpp"
#include "te/core/instrument.hpp"
#include "te/core/result.hpp"
#include "te/feed/bitstamp/snapshot.hpp"
#include "te/feed/captured_events.hpp"

namespace te {

enum class JoinedCaptureError {
    frame_index_ended_early,
    payload_ended_early,
    payload_unreadable,
    frame_index_unreadable,
    frame_malformed,
    unknown_stream_kind,
    seed_unreadable,
    seed_parse_failure,
    checkpoint_unreadable,
    checkpoint_parse_failure,
    order_decode_failure,
    trade_decode_failure,
    fill_decode_failure
};

struct JoinedCapture {
    bitstamp::BookSnapshot seed;
    std::optional<bitstamp::BookSnapshot> checkpoint;
    std::vector<CapturedOrderEvent> jc_captureOrderEvents;
    std::vector<CapturedTradeEvent> jc_tradeEvents;
};

// One segment is one replay epoch; callers load later manifest segments separately.
Result<JoinedCapture, JoinedCaptureError> loadSegment(
    const SegmentDescription& segment, InstrumentSpec spec);

}  // namespace te
