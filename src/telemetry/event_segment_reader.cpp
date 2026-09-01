#include "te/telemetry/event_segment_reader.hpp"

#include <array>
#include <utility>

namespace te {

EventSegmentReader::EventSegmentReader(std::ifstream input, SegmentHeader header)
    : input_{std::move(input)}, header_{header} {}

Result<EventSegmentReader, SegmentReaderError> EventSegmentReader::open(
    const std::filesystem::path& inputPath) {
    std::ifstream input{inputPath, std::ios::binary};
    if (!input.is_open()) {
        return Result<EventSegmentReader, SegmentReaderError>::failure(
            SegmentReaderError::cannot_open);
    }

    std::array<std::byte, kSegmentHeaderSize> headerBytes{};
    input.read(reinterpret_cast<char*>(headerBytes.data()),
               static_cast<std::streamsize>(headerBytes.size()));
    if (input.gcount() != static_cast<std::streamsize>(headerBytes.size())) {
        return Result<EventSegmentReader, SegmentReaderError>::failure(
            SegmentReaderError::header_read_failed);
    }

    const auto decodedHeader = decodeSegmentHeader(headerBytes);
    if (!decodedHeader.hasValue()) {
        return Result<EventSegmentReader, SegmentReaderError>::failure(
            SegmentReaderError::header_decode_failed);
    }

    const SegmentHeader& segmentHeader = *decodedHeader.valueIf();
    if (segmentHeader.recordSize != kEventRecordSize) {
        return Result<EventSegmentReader, SegmentReaderError>::failure(
            SegmentReaderError::header_decode_failed);
    }

    EventSegmentReader reader{std::move(input), segmentHeader};
    reader.stats_.bytesRead = kSegmentHeaderSize;

    return Result<EventSegmentReader, SegmentReaderError>::success(
        std::move(reader));
}

Result<std::optional<DecodedEventRecord>, SegmentReaderError>
EventSegmentReader::next() {
    if (finished_) {
        return Result<std::optional<DecodedEventRecord>, SegmentReaderError>::failure(
            SegmentReaderError::reader_finished);
    }

    std::array<std::byte, kEventRecordSize> recordBytes{};
    input_.read(reinterpret_cast<char*>(recordBytes.data()),
                static_cast<std::streamsize>(recordBytes.size()));
    const std::streamsize bytesReceived = input_.gcount();

    if (bytesReceived == 0 && input_.eof()) {
        finished_ = true;
        return Result<std::optional<DecodedEventRecord>, SegmentReaderError>::success(
            std::nullopt);
    }

    if (bytesReceived != static_cast<std::streamsize>(recordBytes.size())) {
        finished_ = true;
        const SegmentReaderError error = input_.eof()
                                             ? SegmentReaderError::truncated_record
                                             : SegmentReaderError::record_read_failed;
        return Result<std::optional<DecodedEventRecord>, SegmentReaderError>::failure(error);
    }

    const auto decodedRecord = decodeEventRecord(recordBytes);
    if (!decodedRecord.hasValue()) {
        finished_ = true;
        return Result<std::optional<DecodedEventRecord>, SegmentReaderError>::failure(
            SegmentReaderError::record_decode_failed);
    }

    ++stats_.recordsRead;
    stats_.bytesRead += recordBytes.size();
    return Result<std::optional<DecodedEventRecord>, SegmentReaderError>::success(
        std::optional<DecodedEventRecord>{*decodedRecord.valueIf()});
}

}  // namespace te
