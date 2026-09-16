#pragma once

#include <te/capture/manifest_reader.hpp>
#include <te/capture/segment_loader.hpp>
#include <te/core/result.hpp>

namespace te {

enum class ValidationError {
    payload_size_mismatch,
    payload_hash_mismatch,
    frame_index_size_mismatch,
    frame_index_hash_mismatch,
    frame_count_mismatch,
    order_event_count_mismatch,
    trade_event_count_mismatch,
    control_frame_count_mismatch,
};

// Proves a JoinedCapture matches what its own manifest declared. The private constructor means the
// only way to obtain one is validateCapture() succeeding.
//
// Enforcement lives in captureCoordinator(), not in Replay::replay()'s signature. replay() takes a
// seed plus two event vectors and is called directly from ~20 test sites that build synthetic
// events in memory to test merge ordering -- data that has no manifest to be validated against.
// Requiring ValidatedCapture there would force those tests through capture machinery irrelevant to
// what they check, and would conflate "is the merge algorithm correct" with "is this capture
// complete". captureCoordinator is the only path from a capture directory to a replay, so gating
// it there closes the real hole without that cost.
class ValidatedCapture {
public:
    const JoinedCapture& capture() const { return capture_; }

private:
    friend Result<ValidatedCapture, ValidationError> validateCapture(
                                    JoinedCapture capture, const SegmentDescription& declared);

    explicit ValidatedCapture(JoinedCapture capture) : capture_(std::move(capture)) {}

    JoinedCapture capture_;
};

// Compares what `capture` actually holds against what `declared` says the manifest promised.
// Every mismatch gets its own named ValidationError.
//
// Not checked here: `declaredChainValid` (read into the manifest but never compared) and the
// recorder's `status` field (not read into SegmentDescription at all). Note also that loader-side
// hash/size I/O failures are silent -- segment_loader leaves actualPayloadSha256 empty and the
// byte count at 0 rather than raising a named error, which fails closed against any real manifest
// but is not itself a reported loader error.
Result<ValidatedCapture, ValidationError> validateCapture(
    JoinedCapture capture, const SegmentDescription& declared);

}  // namespace te
