#pragma once

#include <cstdint>
#include <optional>
#include <string>
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
};

struct JoinedCapture {
    bitstamp::BookSnapshot seed;
    std::optional<bitstamp::BookSnapshot> checkpoint;
    std::vector<CapturedOrderEvent> jc_captureOrderEvents;
    std::vector<CapturedTradeEvent> jc_tradeEvents;

    // What loadSegment actually measured while streaming the payload and frame index files --
    // facts about what was read, not a judgment about whether they match the manifest. That
    // comparison is validateCapture()'s job, not this struct's.
    std::uint64_t actualPayloadBytes{};
    std::uint64_t actualFrameIndexBytes{};
    std::uint64_t actualControlFrameCount{};

    // Hashed from a separate full read of each file rather than from the getline() loop, which
    // strips newlines and would hash a different byte sequence than the recorder did. Left empty
    // if that read fails, which fails closed against any real manifest; see loadSegment.
    std::string actualPayloadSha256;
    std::string actualFrameIndexSha256;
};

// One segment is one replay epoch; callers load later manifest segments separately.
Result<JoinedCapture, JoinedCaptureError> loadSegment(
    const SegmentDescription& segment, InstrumentSpec spec);

}  // namespace te
