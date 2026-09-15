#pragma once

#include <te/capture/manifest_reader.hpp>
#include <te/capture/segment_loader.hpp>
#include <te/core/result.hpp>

namespace te {

enum class ValidationError {
    // TODO(fuaad): name one variant per way a loaded capture can fail to match what the
    // manifest declared. Candidates from the design discussion: payload size mismatch, payload
    // hash mismatch, frame index size/hash mismatch, declared vs. actual frame/order/trade
    // count mismatch, chain_valid false. Decide per-field granularity vs. one generic mismatch.
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
