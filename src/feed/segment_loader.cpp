#include "te/feed/segment_loader.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "simdjson/ondemand.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/padded_string_view.h"
#include "te/feed/bitstamp/decoder.hpp"
#include "te/feed/bitstamp/trade_decoder.hpp"

namespace te {
namespace {

Result<std::string, JoinedCaptureError> readTextFile(
    const std::filesystem::path& path, JoinedCaptureError unreadableError) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return Result<std::string, JoinedCaptureError>::failure(unreadableError);
    }

    std::ostringstream contents;
    contents << file.rdbuf();
    return Result<std::string, JoinedCaptureError>::success(contents.str());
}

Result<bitstamp::BookSnapshot, JoinedCaptureError> loadSnapshot(
    const std::filesystem::path& snapshotPath,
    InstrumentSpec spec,
    JoinedCaptureError unreadableError,
    JoinedCaptureError parseError) {
    const auto snapshotTextResult = readTextFile(snapshotPath, unreadableError);
    if (!snapshotTextResult.hasValue()) {
        return Result<bitstamp::BookSnapshot, JoinedCaptureError>::failure(
            *snapshotTextResult.errorIf());
    }

    const auto parsedSnapshot = bitstamp::parseSnapshot(*snapshotTextResult.valueIf(), spec);
    if (!parsedSnapshot.hasValue()) {
        return Result<bitstamp::BookSnapshot, JoinedCaptureError>::failure(parseError);
    }

    return Result<bitstamp::BookSnapshot, JoinedCaptureError>::success(
        *parsedSnapshot.valueIf());
}

}  // namespace

Result<JoinedCapture, JoinedCaptureError> loadSegment(
    const SegmentDescription& segment, InstrumentSpec spec) {
    JoinedCapture joinedCapture;

    const auto seedSnapshotResult =
        loadSnapshot(segment.seedPath, spec, JoinedCaptureError::seed_unreadable,
                     JoinedCaptureError::seed_parse_failure);
    if (!seedSnapshotResult.hasValue()) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            *seedSnapshotResult.errorIf());
    }
    joinedCapture.seed = *seedSnapshotResult.valueIf();

    if (segment.checkpointPath.has_value()) {
        const auto checkpointSnapshotResult =
            loadSnapshot(*segment.checkpointPath, spec,
                         JoinedCaptureError::checkpoint_unreadable,
                         JoinedCaptureError::checkpoint_parse_failure);
        if (!checkpointSnapshotResult.hasValue()) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                *checkpointSnapshotResult.errorIf());
        }
        joinedCapture.checkpoint = *checkpointSnapshotResult.valueIf();
    }

    std::ifstream payloadInput{segment.payloadPath, std::ios::binary};
    if (!payloadInput) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            JoinedCaptureError::payload_unreadable);
    }

    std::ifstream frameIndexInput{segment.frameIndexPath, std::ios::binary};
    if (!frameIndexInput) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            JoinedCaptureError::frame_index_unreadable);
    }

    simdjson::ondemand::parser frameParser;
    std::string payloadLine;
    std::string frameIndexLine;

    while (true) {
        const bool hasPayloadLine =
            static_cast<bool>(std::getline(payloadInput, payloadLine));
        const bool hasFrameIndexLine =
            static_cast<bool>(std::getline(frameIndexInput, frameIndexLine));

        if (!hasPayloadLine && !hasFrameIndexLine) {
            break;
        }
        if (hasPayloadLine && !hasFrameIndexLine) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::frame_index_ended_early);
        }
        if (!hasPayloadLine && hasFrameIndexLine) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::payload_ended_early);
        }

        simdjson::padded_string frameBuffer{frameIndexLine};
        simdjson::ondemand::document frameDocument;
        if (frameParser.iterate(frameBuffer).get(frameDocument)) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::frame_malformed);
        }

        std::string_view streamKind;
        if (frameDocument["streamKind"].get_string().get(streamKind)) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::frame_malformed);
        }

        std::uint64_t captureOrdinal{};
        if (frameDocument["captureOrdinal"].get_uint64().get(captureOrdinal)) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::frame_malformed);
        }

        if (streamKind == "order") {
            const auto decodedOrder = bitstamp::decodeOrder(payloadLine, spec);
            const auto decodedFill = bitstamp::decodeFill(payloadLine, spec);
            if (!decodedOrder.hasValue()) {
                return Result<JoinedCapture, JoinedCaptureError>::failure(
                    JoinedCaptureError::order_decode_failure);
            }
            if (!decodedFill.hasValue()) {
                return Result<JoinedCapture, JoinedCaptureError>::failure(
                    JoinedCaptureError::fill_decode_failure);
            }
            joinedCapture.jc_captureOrderEvents.push_back(CapturedOrderEvent{
                *decodedOrder.valueIf(), *decodedFill.valueIf(), captureOrdinal});
        } else if (streamKind == "trade") {
            const auto decodedTrade = bitstamp::decodeTrade(payloadLine, spec);
            if (!decodedTrade.hasValue()) {
                return Result<JoinedCapture, JoinedCaptureError>::failure(
                    JoinedCaptureError::trade_decode_failure);
            }
            joinedCapture.jc_tradeEvents.push_back(
                CapturedTradeEvent{*decodedTrade.valueIf(), captureOrdinal});
        } else if (streamKind != "control") {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::unknown_stream_kind);
        }
    }

    return Result<JoinedCapture, JoinedCaptureError>::success(std::move(joinedCapture));
}

}  // namespace te
