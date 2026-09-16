#include <te/capture/capture_validator.hpp>

namespace te {
Result<ValidatedCapture, ValidationError> validateCapture(
    JoinedCapture capture, const SegmentDescription& declared)
{
    

    if (capture.actualPayloadBytes != declared.declaredPayloadBytes) {
        return Result<ValidatedCapture, ValidationError>::failure
        (te::ValidationError::payload_size_mismatch);
    };

    if (capture.actualPayloadSha256 != declared.declaredPayloadSha256) {
        return Result<ValidatedCapture, ValidationError>::failure(
            ValidationError::payload_hash_mismatch);
    }

    if (capture.actualFrameIndexBytes != declared.declaredFrameIndexBytes) {
        return Result<ValidatedCapture, ValidationError>::failure(
            te::ValidationError::frame_index_size_mismatch);

    };
    if (capture.actualFrameIndexSha256 != declared.declaredFrameIndexSha256) {
        return Result<ValidatedCapture, ValidationError>::failure(
            ValidationError::frame_index_hash_mismatch);
    }
    if (capture.actualControlFrameCount != declared.declaredControlFrameCount) {
        return Result<ValidatedCapture, ValidationError>::failure(
            te::ValidationError::control_frame_count_mismatch);

    };
    if (capture.jc_captureOrderEvents.size() != declared.declaredOrderEventCount) {
        return Result<ValidatedCapture, ValidationError>::failure(
            te::ValidationError::order_event_count_mismatch);

    };
    if (capture.jc_tradeEvents.size() != declared.declaredTradeEventCount) {
        return Result<ValidatedCapture, ValidationError>::failure(
            te::ValidationError::trade_event_count_mismatch);

    };

    const std::uint64_t actualFrameCount =
        capture.jc_captureOrderEvents.size() + capture.jc_tradeEvents.size() +
        capture.actualControlFrameCount;
    if (actualFrameCount != declared.declaredFrameCount) {
        return Result<ValidatedCapture, ValidationError>::failure(
            ValidationError::frame_count_mismatch);
    }

    ValidatedCapture validatedCapture(std:: move(capture));
    return Result<ValidatedCapture, ValidationError>::success(
        std:: move((validatedCapture)));
     
        
};
}
