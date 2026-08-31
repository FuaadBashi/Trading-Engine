#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>

#include "te/core/result.hpp"
#include "te/telemetry/event_record_format.hpp"
#include "te/telemetry/segment_format.hpp"

namespace te {

enum class SegmentWriterError {
    output_already_exists,
    cannot_open,
    header_encode_failed,
    header_write_failed,
    record_encode_failed,
    record_write_failed,
    writer_finished,
    finish_failed,
};

struct SegmentWriterStats {
    std::uint64_t recordsWritten{};

    // Includes the fixed header and every complete record accepted by the stream.
    std::uint64_t bytesWritten{};
};

// Streaming owner of one immutable event-segment file. A successful open() means the header is
// already on disk; append() then preserves the order in which mixed records reach the writer.
class EventSegmentWriter {
public:
    [[nodiscard]] static Result<EventSegmentWriter, SegmentWriterError> open(
        const std::filesystem::path& outputPath, const SegmentHeader& header);

    [[nodiscard]] Result<std::size_t, SegmentWriterError> append(
        const DecodedEventRecord& record);

    // Flushes and closes explicitly so final I/O failure can be reported instead of being hidden
    // inside ofstream's destructor. A successful result is the final immutable accounting.
    [[nodiscard]] Result<SegmentWriterStats, SegmentWriterError> finish();

    const SegmentWriterStats& stats() const noexcept { return stats_; }
    bool isFinished() const noexcept { return finished_; }

    EventSegmentWriter(EventSegmentWriter&&) noexcept = default;
    EventSegmentWriter& operator=(EventSegmentWriter&&) noexcept = default;
    EventSegmentWriter(const EventSegmentWriter&) = delete;
    EventSegmentWriter& operator=(const EventSegmentWriter&) = delete;

private:
    explicit EventSegmentWriter(std::ofstream output);

    std::ofstream output_;
    SegmentWriterStats stats_{};
    bool finished_{};
};

}  // namespace te
