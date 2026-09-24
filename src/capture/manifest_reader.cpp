#include "te/capture/manifest_reader.hpp"

#include <fstream>
#include <sstream>

// Not <simdjson.h>: in 3.9.1 it pulls in the DOM serializer, which fails on current clang.
#include "simdjson/ondemand.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/padded_string_view.h"
#include "te/core/text_to_int.hpp"


namespace te {

namespace {

// SHA-256 hex digests are exactly 64 lowercase-or-uppercase hex characters. This is a format
// check only -- it does not confirm the hash is correct, only that it is shaped like one.
bool isValidSha256Hex(std::string_view text) {
    constexpr std::size_t kSha256HexLength = 64;
    if (text.size() != kSha256HexLength) {
        return false;
    }
    for (char c : text) {
        const bool isHexDigit =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!isHexDigit) {
            return false;
        }
    }
    return true;
}

}  // namespace

Result<CaptureManifest, ManifestError> manifestReader(const std::filesystem::path& captureDirectory)
{

    const std::filesystem::path manifestPath = captureDirectory / "manifest.json";
    CaptureManifest captureManifest;

    std::ifstream manifestInput{manifestPath, std::ios::binary};
    if (!manifestInput) {
        return Result<CaptureManifest, ManifestError>::failure(
            ManifestError::manifest_unreadable);
    }

    std::ostringstream manifestContents;
    manifestContents << manifestInput.rdbuf();
    const std::string manifestText = manifestContents.str();

    simdjson::ondemand::parser parser;
    simdjson::padded_string buffer = simdjson::padded_string(manifestText);

    simdjson::ondemand::document doc;
    simdjson::error_code err = parser.iterate(buffer).get(doc);
    if (err) {
        return Result<CaptureManifest, ManifestError>::failure(
            ManifestError::manifest_malformed);
    }

    std::uint64_t formatVersion{};
    if (doc["format_version"].get_uint64().get(formatVersion)) {
        return Result<CaptureManifest, ManifestError>::failure(ManifestError::manifest_missing_field);
    }
    if (formatVersion == 2){
        captureManifest.formatVersion = static_cast<std::uint32_t>(formatVersion);
    } else {
        return Result<CaptureManifest, ManifestError>::failure(ManifestError::unsupported_format_version);
    }


    std::string_view venueText;
    if (doc["venue"].get_string().get(venueText)) {
        return Result<CaptureManifest, ManifestError>::failure(ManifestError::manifest_missing_field);
    }
    const auto venue = parseVenueId(venueText);
    if (!venue.hasValue()) {
        return Result<CaptureManifest, ManifestError>::failure(ManifestError::manifest_invalid_structure);
    }
    captureManifest.venue = *venue.valueIf();

    std::string_view instrumentText;
    if (doc["instrument"].get_string().get(instrumentText)) {
        return Result<CaptureManifest, ManifestError>::failure(ManifestError::manifest_missing_field);
    }
    const auto instrument = parseInstrumentId(instrumentText);
    if (!instrument.hasValue()) {
        return Result<CaptureManifest, ManifestError>::failure(ManifestError::manifest_invalid_structure);
    }
    captureManifest.instrument = *instrument.valueIf();



    simdjson::ondemand::array segments;
    err = doc["segments"].get_array().get(segments);
    if (err) {
        return Result<CaptureManifest, ManifestError>::failure(
            ManifestError::manifest_missing_field);
    }
    
    std::string_view payloadName;
    std::string_view frameIndexName;
    std::string_view snapshotName;
    std::string_view checkpointName;
    std::uint64_t manifestIndex{};
    std::uint64_t expectedIndex{};


    for (auto segmentResult : segments) {
        simdjson::ondemand::object segment;
        SegmentDescription segmentDescription;

        err = segmentResult.get_object().get(segment);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
         
        err = segment["index"].get_uint64().get(manifestIndex);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        err = segment["payload"].get_string().get(payloadName);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        err = segment["frame_index"].get_string().get(frameIndexName);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }

        err = segment["snapshot"].get_string().get(snapshotName);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }

        err = segment["checkpoint"].get_string().get(checkpointName);
        if (!err) {
            segmentDescription.checkpointPath =
                captureDirectory / std::filesystem::path{std::string{checkpointName}};
        } else if (err != simdjson::NO_SUCH_FIELD) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_invalid_structure);
        }

        // Declared completeness fields. All nine are required: an older manifest missing any of
        // them cannot be compared against, so a missing field here is an error, not a default.
        std::uint64_t payloadBytes{};
        err = segment["payload_bytes"].get_uint64().get(payloadBytes);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        segmentDescription.declaredPayloadBytes = payloadBytes;

        std::string_view payloadSha256;
        err = segment["payload_sha256"].get_string().get(payloadSha256);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        if (!isValidSha256Hex(payloadSha256)) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_invalid_hash);
        }
        segmentDescription.declaredPayloadSha256 = std::string{payloadSha256};

        std::uint64_t frameIndexBytes{};
        err = segment["frames_bytes"].get_uint64().get(frameIndexBytes);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        segmentDescription.declaredFrameIndexBytes = frameIndexBytes;

        std::string_view frameIndexSha256;
        err = segment["frames_sha256"].get_string().get(frameIndexSha256);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        if (!isValidSha256Hex(frameIndexSha256)) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_invalid_hash);
        }
        segmentDescription.declaredFrameIndexSha256 = std::string{frameIndexSha256};

        std::uint64_t declaredFrameCount{};
        err = segment["frames"].get_uint64().get(declaredFrameCount);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        segmentDescription.declaredFrameCount = declaredFrameCount;

        std::uint64_t declaredOrderEventCount{};
        err = segment["order_events"].get_uint64().get(declaredOrderEventCount);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        segmentDescription.declaredOrderEventCount = declaredOrderEventCount;

        std::uint64_t declaredTradeEventCount{};
        err = segment["trade_events"].get_uint64().get(declaredTradeEventCount);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        segmentDescription.declaredTradeEventCount = declaredTradeEventCount;

        std::uint64_t declaredControlFrameCount{};
        err = segment["control_frames"].get_uint64().get(declaredControlFrameCount);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        segmentDescription.declaredControlFrameCount = declaredControlFrameCount;

        bool declaredChainValid{};
        err = segment["chain_valid"].get_bool().get(declaredChainValid);
        if (err) {
            return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_field);
        }
        segmentDescription.declaredChainValid = declaredChainValid;

        segmentDescription.payloadPath = captureDirectory /std::filesystem::path{std::string{payloadName}};
        segmentDescription.frameIndexPath = captureDirectory /std::filesystem::path{std::string{frameIndexName}};
        segmentDescription.seedPath =captureDirectory / std::filesystem::path{std::string{snapshotName}};
        if(expectedIndex == manifestIndex){
            segmentDescription.index = static_cast<std::size_t>(manifestIndex);
        } else {
            return Result<CaptureManifest, ManifestError>::failure(ManifestError::mismatch_index);
        }
        captureManifest.segments.push_back(segmentDescription);
        ++expectedIndex;
      
    }

    if (captureManifest.segments.empty()){
        return Result<CaptureManifest, ManifestError>::failure(
                ManifestError::manifest_missing_segment);
    }

    return Result<CaptureManifest, ManifestError>::success(captureManifest);
    
};

}
