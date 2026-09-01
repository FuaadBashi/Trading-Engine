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

Result<std::size_t, SegmentWriterError> EventSegmentWriter::append(
    const DecodedEventRecord& record) {
    if (finished_) {
        return Result<std::size_t, SegmentWriterError>::failure(
            SegmentWriterError::writer_finished);
    }

    // Encode before writing so an invalid event cannot add a partial record to the file.
    std::array<std::byte, kEventRecordSize> recordBytes{};
    const auto encoded = encodeEventRecord(record, recordBytes);
    if (!encoded.hasValue()) {
        return Result<std::size_t, SegmentWriterError>::failure(
            SegmentWriterError::record_encode_failed);
    }

    output_.write(reinterpret_cast<const char*>(recordBytes.data()),
                  static_cast<std::streamsize>(recordBytes.size()));
    if (!output_.good()) {
        // Unlike a bad in-memory record, a failed write means the stream itself is broken.
        // Leaving the writer usable would let a caller spin on a doomed file.
        finished_ = true;
        return Result<std::size_t, SegmentWriterError>::failure(
            SegmentWriterError::record_write_failed);
    }

    // Accounting changes only after the complete fixed-size record was accepted by the stream.
    ++stats_.recordsWritten;
    stats_.bytesWritten += recordBytes.size();
    return Result<std::size_t, SegmentWriterError>::success(recordBytes.size());
}

Result<SegmentWriterStats, SegmentWriterError> EventSegmentWriter::finish() {
    if (finished_) {
        return Result<SegmentWriterStats, SegmentWriterError>::failure(
            SegmentWriterError::writer_finished);
    }

    // finish() is terminal even when flushing fails. Retrying on the same stream could make the
    // caller believe a file with an earlier partial write is trustworthy.
    finished_ = true;
    output_.flush();
    const bool flushed = output_.good();
    output_.close();

    if (!flushed || output_.fail()) {
        return Result<SegmentWriterStats, SegmentWriterError>::failure(
            SegmentWriterError::finish_failed);
    }

    return Result<SegmentWriterStats, SegmentWriterError>::success(stats_);
}

}  // namespace te
