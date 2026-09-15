#pragma once

#include <filesystem>
#include <string>
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
    unsupported_format_version,
    manifest_invalid_hash
};

struct SegmentDescription {

    std::size_t index{};
    std::filesystem::path payloadPath;
    std::filesystem::path frameIndexPath;
    std::filesystem::path seedPath;
    std::optional<std::filesystem::path> checkpointPath;

    
    std::uint64_t declaredPayloadBytes{};
    std::string declaredPayloadSha256;
    std::uint64_t declaredFrameIndexBytes{};
    std::string declaredFrameIndexSha256;
    std::uint64_t declaredFrameCount{};
    std::uint64_t declaredOrderEventCount{};
    std::uint64_t declaredTradeEventCount{};
    std::uint64_t declaredControlFrameCount{};
    bool declaredChainValid{};
};

struct CaptureManifest {
    std::uint32_t formatVersion{};
    VenueId venue{VenueId::unknown};
    InstrumentId instrument{InstrumentId::unknown};

    std::vector<SegmentDescription> segments;
};

Result<CaptureManifest, ManifestError> manifestReader(const std::filesystem::path& captureDirectory);


}