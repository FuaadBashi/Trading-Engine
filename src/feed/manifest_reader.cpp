#include "te/feed/manifest_reader.hpp"

#include <fstream>
#include <sstream>

#include "simdjson/ondemand.h"
#include "simdjson/padded_string-inl.h"
#include "simdjson/padded_string.h"
#include "simdjson/padded_string_view-inl.h"
#include "simdjson/padded_string_view.h"
#include "te/core/text_to_int.hpp"


namespace te {
    
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
