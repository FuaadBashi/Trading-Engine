#pragma once

#include <filesystem>
#include <vector>
#include <te/feed/bitstamp/snapshot.hpp>
#include <te/feed/events.hpp>
#include <te/feed/trade_event.hpp>
#include <te/core/instrument.hpp>


namespace te::bitstamp {

enum class JoinedCaptureError {

// raw has a line but frame index ended
    frame_index_ended_early,

// frame index has a line but raw ended
    payload_ended_early,
    payload_unreadable,
    payload_cant_be_parsed,


// frame JSON is malformed
    frame_malformed,
    frame_index_unreadable,
    frame_cant_be_parsed,

// frame streamKind is unknown
    unknown_stream_kind,
    manifest_missing_field,

// manisfest issues
    manifest_unreadable,
    manifest_malformed,
    manifest_missing_segment,
    manifest_invalid_structure,

    checkpoint_cant_be_parsed,
    checkpoint_unreadable,
    seed_unreadable,
    seed_cant_be_parsed

};

struct JoinedCapture{
    BookSnapshot seed;
    BookSnapshot checkpoint;
    std::vector<OrderEvent> orderEvents;
    std::vector<TradeEvent> tradeEvents;
};

    Result<JoinedCapture, JoinedCaptureError> loadJoinedCapture(const std::filesystem::path& captureDirectory, InstrumentSpec spec);


}  // namespace te::bitstamp
