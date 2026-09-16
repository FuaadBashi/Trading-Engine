#pragma once

#include <te/capture/manifest_reader.hpp>
#include <te/capture/segment_loader.hpp>
#include <te/core/result.hpp>

namespace te {

enum class ValidationError {
    payload_size_mismatch,
    frame_index_size_mismatch,
    frame_count_mismatch,
    order_event_count_mismatch,
    trade_event_count_mismatch,
    control_frame_count_mismatch,
};

// Proves a JoinedCapture matches what its own manifest declared. The private constructor means
// the only way to obtain one is validateCapture() succeeding. Replay::replay() should be changed
// to require this type instead of a raw JoinedCapture once this exists, so nothing can reach
// replay without passing the check.
class ValidatedCapture {
public:
    const JoinedCapture& capture() const { return capture_; }

private:
    friend Result<ValidatedCapture, ValidationError> validateCapture(
                                    JoinedCapture capture, const SegmentDescription& declared);

    explicit ValidatedCapture(JoinedCapture capture) : capture_(std::move(capture)) {}

    JoinedCapture capture_;
};

// TODO(fuaad): write this yourself. Compare what `capture` actually holds against what
// `declared` says the manifest promised. Every mismatch gets its own named ValidationError.
Result<ValidatedCapture, ValidationError> validateCapture(
    JoinedCapture capture, const SegmentDescription& declared);

}  // namespace te
