#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>

#include "te/core/result.hpp"
#include "te/telemetry/event_record_format.hpp"
#include "te/telemetry/segment_format.hpp"

namespace te {

enum class SegmentReaderError {
    cannot_open,
    header_read_failed,
    header_decode_failed,
    record_read_failed,
    truncated_record,
    record_decode_failed,
    reader_finished,
};

struct SegmentReaderStats {
    std::uint64_t recordsRead{};
    // Includes the validated fixed header and every complete decoded record.
    std::uint64_t bytesRead{};
};

// Streaming owner of one event-segment file. open() validates the header before returning;
// next() then decodes records in their original on-disk order without loading the whole file.
class EventSegmentReader {
public:
    [[nodiscard]] static Result<EventSegmentReader, SegmentReaderError> open(
        const std::filesystem::path& inputPath);

    // A successful value containing a record means one record was consumed. A successful
    // std::nullopt means clean end-of-file. Failures distinguish truncation from bad encoding.
    [[nodiscard]] Result<std::optional<DecodedEventRecord>, SegmentReaderError> next();

    const SegmentHeader& header() const noexcept { return header_; }
    const SegmentReaderStats& stats() const noexcept { return stats_; }
    bool isFinished() const noexcept { return finished_; }

    EventSegmentReader(EventSegmentReader&&) noexcept = default;
    EventSegmentReader& operator=(EventSegmentReader&&) noexcept = default;
    EventSegmentReader(const EventSegmentReader&) = delete;
    EventSegmentReader& operator=(const EventSegmentReader&) = delete;

private:
    EventSegmentReader(std::ifstream input, SegmentHeader header);

    std::ifstream input_;
    SegmentHeader header_;
    SegmentReaderStats stats_{};
    bool finished_{};
};

}  // namespace te
