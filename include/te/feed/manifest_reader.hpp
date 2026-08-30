#pragma once

#include <filesystem>
#include <vector>
#include "te/core/instrument.hpp"
#include "te/core/result.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace te {

enum class ManifestError {
    manifest_unreadable,
    manifest_malformed,
    manifest_missing_field,
    manifest_missing_segment,
    manifest_invalid_structure,
    mismatch_index,
    unsupported_format_version
};

struct SegmentDescription {

    std::size_t index{};
    std::filesystem::path payloadPath;
    std::filesystem::path frameIndexPath;
    std::filesystem::path seedPath;
    std::optional<std::filesystem::path> checkpointPath;
};

struct CaptureManifest {
    std::uint32_t formatVersion{};
    VenueId venue{VenueId::unknown};
    InstrumentId instrument{InstrumentId::unknown};

    std::vector<SegmentDescription> segments;
};

Result<CaptureManifest, ManifestError> manifestReader(const std::filesystem::path& captureDirectory);


}