#include "te/telemetry/event_segment_writer.hpp"

#include <array>
#include <system_error>
#include <utility>

namespace te {

EventSegmentWriter::EventSegmentWriter(std::ofstream output)
    : output_{std::move(output)} {}

Result<EventSegmentWriter, SegmentWriterError> EventSegmentWriter::open(
    const std::filesystem::path& outputPath, const SegmentHeader& header) {
    // This writer produces event segments only, even though the shared header codec also supports
    // the separate fixed-size snapshot record format.
    if (header.recordSize != kEventRecordSize) {
        return Result<EventSegmentWriter, SegmentWriterError>::failure(
            SegmentWriterError::header_encode_failed);
    }

    // Validate and encode before touching the filesystem. Invalid metadata must not leave an
    // empty file that could later be mistaken for a capture segment.
    std::array<std::byte, kSegmentHeaderSize> headerBytes{};
    const auto encoded = encodeSegmentHeader(header, headerBytes);
    if (!encoded.hasValue()) {
        return Result<EventSegmentWriter, SegmentWriterError>::failure(
            SegmentWriterError::header_encode_failed);
    }

    std::error_code filesystemError;
    if (std::filesystem::exists(outputPath, filesystemError)) {
        return Result<EventSegmentWriter, SegmentWriterError>::failure(
            SegmentWriterError::output_already_exists);
    }
    if (filesystemError) {
        return Result<EventSegmentWriter, SegmentWriterError>::failure(
            SegmentWriterError::cannot_open);
    }

    std::ofstream output{outputPath, std::ios::binary | std::ios::trunc};
    if (!output.is_open()) {
        return Result<EventSegmentWriter, SegmentWriterError>::failure(
            SegmentWriterError::cannot_open);
    }

    output.write(reinterpret_cast<const char*>(headerBytes.data()),
                 static_cast<std::streamsize>(headerBytes.size()));
    output.flush();
    if (!output.good()) {
        output.close();
        std::error_code ignored;
        std::filesystem::remove(outputPath, ignored);
        return Result<EventSegmentWriter, SegmentWriterError>::failure(
            SegmentWriterError::header_write_failed);
    }

    EventSegmentWriter writer{std::move(output)};
    writer.stats_.bytesWritten = kSegmentHeaderSize;
    return Result<EventSegmentWriter, SegmentWriterError>::success(std::move(writer));
}

}  // namespace te
