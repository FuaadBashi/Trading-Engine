#include "te/capture/segment_loader.hpp"

#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// Not <simdjson.h>: in 3.9.1 it pulls in the DOM serializer, which fails on current clang.
#include "simdjson/ondemand.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/padded_string_view.h"
#include "te/core/sha256.hpp"
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

// Reads the whole file as raw bytes and hashes exactly what is on disk. Deliberately not folded
// into the payload/frame-index getline() loop below: getline() strips the newline delimiter from
// each line it returns, so hashing the reconstructed lines would hash a different byte sequence
// than the file actually contains -- and a different sequence than the recorder hashed when it
// wrote payload_sha256/frames_sha256 into the manifest. This costs a second read of each file;
// correctness of the comparison matters more than saving one pass over an offline capture file.
Result<std::string, JoinedCaptureError> hashFile(
    const std::filesystem::path& path, JoinedCaptureError unreadableError) {
    const auto contents = readTextFile(path, unreadableError);
    if (!contents.hasValue()) {
        return Result<std::string, JoinedCaptureError>::failure(*contents.errorIf());
    }
    const std::string& text = *contents.valueIf();
    return Result<std::string, JoinedCaptureError>::success(
        sha256Hex(std::as_bytes(std::span{text.data(), text.size()})));
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

    // Exact on-disk size, not a running total reconstructed from getline()'d lines -- that would
    // be off by one per line depending on trailing newline and line-ending convention. Both
    // ifstreams above already proved these paths exist, so a stat failure here would mean the
    // file vanished in the moment between opening it and this call; that race is not currently
    // surfaced as its own error and simply leaves the count at zero.
    std::error_code sizeError;
    const auto payloadSize = std::filesystem::file_size(segment.payloadPath, sizeError);
    if (!sizeError) {
        joinedCapture.actualPayloadBytes = payloadSize;
    }
    const auto frameIndexSize = std::filesystem::file_size(segment.frameIndexPath, sizeError);
    if (!sizeError) {
        joinedCapture.actualFrameIndexBytes = frameIndexSize;
    }

    // Same soft-fail posture as the size lookup above: both ifstreams already proved these paths
    // are readable, so a failure here is the same vanishingly rare TOCTOU race, not a new error
    // path. Left empty on failure rather than surfaced as a hard error.
    const auto payloadHash = hashFile(segment.payloadPath, JoinedCaptureError::payload_unreadable);
    if (payloadHash.hasValue()) {
        joinedCapture.actualPayloadSha256 = *payloadHash.valueIf();
    }
    const auto frameIndexHash =
        hashFile(segment.frameIndexPath, JoinedCaptureError::frame_index_unreadable);
    if (frameIndexHash.hasValue()) {
        joinedCapture.actualFrameIndexSha256 = *frameIndexHash.valueIf();
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
            const auto decodedEvent = bitstamp::decodeCapturedOrder(payloadLine, spec);

            if (!decodedEvent.hasValue()) {
                return Result<JoinedCapture, JoinedCaptureError>::failure(
                    JoinedCaptureError::order_decode_failure);
            }

            const auto decodedOrder = decodedEvent.valueIf()->event;
            const auto decodedFill = decodedEvent.valueIf()->amountTraded;
            joinedCapture.jc_captureOrderEvents.push_back(CapturedOrderEvent{
                decodedOrder, decodedFill, captureOrdinal});
        } else if (streamKind == "trade") {
            const auto decodedTrade = bitstamp::decodeTrade(payloadLine, spec);
            if (!decodedTrade.hasValue()) {
                return Result<JoinedCapture, JoinedCaptureError>::failure(
                    JoinedCaptureError::trade_decode_failure);
            }
            joinedCapture.jc_tradeEvents.push_back(
                CapturedTradeEvent{*decodedTrade.valueIf(), captureOrdinal});
        } else if (streamKind == "control") {
            ++joinedCapture.actualControlFrameCount;
        } else {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::unknown_stream_kind);
        }
    }

    return Result<JoinedCapture, JoinedCaptureError>::success(std::move(joinedCapture));
}

}  // namespace te
