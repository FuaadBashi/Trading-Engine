#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <te/core/result.hpp>
#include <te/feed/bitstamp/decoder.hpp>
#include <te/feed/bitstamp/joined_capture.hpp>
#include <te/feed/bitstamp/trade_decoder.hpp>
#include <utility>

#include "simdjson/ondemand.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/padded_string_view.h"

namespace te::bitstamp {
namespace {

Result<std::string, JoinedCaptureError> readTextFile(const std::filesystem::path& path,
                                                     JoinedCaptureError unreadableError) {
    std::ifstream file{path, std::ios::binary};
    if (!file) {
        return Result<std::string, JoinedCaptureError>::failure(unreadableError);
    }

    std::ostringstream contents;
    contents << file.rdbuf();

    return Result<std::string, JoinedCaptureError>::success(contents.str());
}

Result<BookSnapshot, JoinedCaptureError> loadSnapshot(const std::filesystem::path& snapshotPath,
                                                      InstrumentSpec spec,
                                                      JoinedCaptureError unreadableError,
                                                      JoinedCaptureError parseError) {
    const auto snapshotTextResult = readTextFile(snapshotPath, unreadableError);

    if (!snapshotTextResult.hasValue()) {
        return Result<BookSnapshot, JoinedCaptureError>::failure(*snapshotTextResult.errorIf());
    }

    const auto parsedSnapshot = parseSnapshot(*snapshotTextResult.valueIf(), spec);
    if (!parsedSnapshot.hasValue()) {
        return Result<BookSnapshot, JoinedCaptureError>::failure(parseError);
    }

    return Result<BookSnapshot, JoinedCaptureError>::success(*parsedSnapshot.valueIf());
}

}  // namespace

Result<JoinedCapture, JoinedCaptureError> loadJoinedCapture(
    const std::filesystem::path& captureDirectory, InstrumentSpec spec) {
    // 1. Read manifest. [x]
    // 2. Find S0, S1, raw JSONL, and frame-index JSONL. [x]
    // 3. Parse S0 and S1 using parseSnapshot. [x]
    // 4. Open raw and frame files.[x]
    // 5. Read them in pairs until both end.[x]
    // 6. Reject if only one file has a next line.[x]
    // 7. Parse frame JSON.[x]
    // 8. Check streamKind.[x]
    // 9. order → decodeOrder → append jc_orderEvents.[x]
    // 10. trade → decodeTrade → append jc_tradeEvents.[x]
    // 11. control → skip.[x]
    // 12. Return the completed JoinedCapture.

    // If captureDirectory is data/raw/bitstamp-btcusd-20260822T000512Z,
    // manifestPath becomes
    // data/raw/bitstamp-btcusd-20260822T000512Z/manifest.json.
    const std::filesystem::path manifestPath = captureDirectory / "manifest.json";
    JoinedCapture joinedCapture;

    std::ifstream manifestInput{manifestPath, std::ios::binary};
    if (!manifestInput) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            JoinedCaptureError::manifest_unreadable);
    }

    std::ostringstream manifestContents;
    manifestContents << manifestInput.rdbuf();
    const std::string manifestText = manifestContents.str();

    simdjson::ondemand::parser parser;
    simdjson::padded_string buffer = simdjson::padded_string(manifestText);

    simdjson::ondemand::document doc;
    simdjson::error_code err = parser.iterate(buffer).get(doc);
    if (err) {
        // couldn't even parse this as JSON at all
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            JoinedCaptureError::manifest_malformed);
    }

    simdjson::ondemand::array segments;

    err = doc["segments"].get_array().get(segments);
    if (err) {
        // "segments" missing or not an array
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            JoinedCaptureError::manifest_missing_field);
    }

    bool foundSegment = false;
    std::string_view payloadName;
    std::string_view frameIndexName;
    std::string_view snapshotName;
    std::string_view checkpointName;

    for (auto segmentResult : segments) {
        simdjson::ondemand::object segment;

        err = segmentResult.get_object().get(segment);
        if (err) {
            // array item is not { ... }
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::manifest_missing_field);
        }
        err = segment["payload"].get_string().get(payloadName);
        if (err) {
            // no payload" field at all
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::manifest_missing_field);
        }
        err = segment["frame_index"].get_string().get(frameIndexName);
        if (err) {
            // no payload" field at all
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::manifest_missing_field);
        }

        err = segment["snapshot"].get_string().get(snapshotName);
        if (err) {
            // no payload" field at all
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::manifest_missing_field);
        }

        err = segment["checkpoint"].get_string().get(checkpointName);
        if (err) {
            // no payload" field at all
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::manifest_missing_field);
        }
        foundSegment = true;
        break;
    }
    if (!foundSegment) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            JoinedCaptureError::manifest_missing_segment);
    }

    const std::filesystem::path payloadPath =
        captureDirectory / std::filesystem::path{std::string{payloadName}};
    const std::filesystem::path frameIndexPath =
        captureDirectory / std::filesystem::path{std::string{frameIndexName}};
    const std::filesystem::path seedPath =
        captureDirectory / std::filesystem::path{std::string{snapshotName}};
    const std::filesystem::path checkpointPath =
        captureDirectory / std::filesystem::path{std::string{checkpointName}};

    const auto seedSnapshotResult =
        loadSnapshot(seedPath, spec, JoinedCaptureError::seed_unreadable,
                     JoinedCaptureError::seed_parse_failure);
    if (!seedSnapshotResult.hasValue()) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(*seedSnapshotResult.errorIf());
    }
    joinedCapture.seed = *seedSnapshotResult.valueIf();

    const auto checkpointSnapshotResult =
        loadSnapshot(checkpointPath, spec, JoinedCaptureError::checkpoint_unreadable,
                     JoinedCaptureError::checkpoint_parse_failure);
    if (!checkpointSnapshotResult.hasValue()) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            *checkpointSnapshotResult.errorIf());
    }
    joinedCapture.checkpoint = *checkpointSnapshotResult.valueIf();

    std::ifstream payloadInput{payloadPath, std::ios::binary};
    if (!payloadInput) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            JoinedCaptureError::payload_unreadable);
    }

    std::ifstream frameIndexInput{frameIndexPath, std::ios::binary};
    if (!frameIndexInput) {
        return Result<JoinedCapture, JoinedCaptureError>::failure(
            JoinedCaptureError::frame_index_unreadable);
    }

    simdjson::ondemand::parser frameParser;
    std::string payloadLine;
    std::string frameIndexLine;

    while (true) {
        const bool hasPayloadLine = static_cast<bool>(std::getline(payloadInput, payloadLine));

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

        const simdjson::error_code frameError = frameParser.iterate(frameBuffer).get(frameDocument);

        if (frameError) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::frame_malformed);
        }
        std::string_view streamKind;
        if (frameDocument["streamKind"].get_string().get(streamKind)) {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::frame_malformed);
        }
        if (streamKind == "order") {
            const auto decodedOrder = decodeOrder(payloadLine, spec);
            if (!decodedOrder.hasValue()) {
                return Result<JoinedCapture, JoinedCaptureError>::failure(
                    JoinedCaptureError::order_decode_failure);
            }
            joinedCapture.jc_orderEvents.push_back(*decodedOrder.valueIf());
        } else if (streamKind == "trade") {
            const auto decodedTrade = decodeTrade(payloadLine, spec);
            if (!decodedTrade.hasValue()) {
                return Result<JoinedCapture, JoinedCaptureError>::failure(
                    JoinedCaptureError::trade_decode_failure);
            }
            joinedCapture.jc_tradeEvents.push_back(*decodedTrade.valueIf());
        } else if (streamKind == "control") {
            // Subscription confirmations: skip.
        } else {
            return Result<JoinedCapture, JoinedCaptureError>::failure(
                JoinedCaptureError::unknown_stream_kind);
        }
    }

    return Result<JoinedCapture, JoinedCaptureError>::success(std::move(joinedCapture));
}
}  // namespace te::bitstamp
