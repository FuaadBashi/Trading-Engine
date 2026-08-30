#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "te/core/instrument.hpp"
#include "te/core/result.hpp"
#include "te/feed/bitstamp/snapshot.hpp"
#include "te/feed/events.hpp"
#include "te/feed/manifest_reader.hpp"
#include "te/feed/trade_event.hpp"

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

struct CapturedOrderEvent {
    OrderEvent event;
    Qty amountTraded;
    std::uint64_t captureOrdinal{};
};

struct CapturedTradeEvent {
    TradeEvent event;
    std::uint64_t captureOrdinal{};
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

namespace te::bitstamp {

using ::te::CapturedOrderEvent;
using ::te::CapturedTradeEvent;


}  // namespace te::bitstamp
