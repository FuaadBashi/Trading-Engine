#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <variant>

#include "te/telemetry/event_segment_reader.hpp"
#include "te/telemetry/event_segment_writer.hpp"

namespace {

class TempReaderDirectory {
public:
    explicit TempReaderDirectory(std::string_view name)
        : path_{std::filesystem::temp_directory_path() / name} {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempReaderDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

te::SegmentHeader makeHeader() {
    te::SegmentHeader header;
    header.recordSize = te::kEventRecordSize;
    header.instrumentSpec = te::InstrumentSpec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    header.seedTimestampMicros = 100;
    header.creationTimestampMicros = 200;
    return header;
}

te::DecodedEventRecord makeOrderRecord() {
    return te::DecodedOrderRecord{
        .event =
            te::OrderEvent{
                .venue_timestamp_us = 101,
                .order_id = te::OrderId{42},
                .price = te::Price{12345},
                .quantity = te::Qty{60000000},
                .side = te::Side::buy,
                .kind = te::EventKind::modify,
            },
        .amountTraded = te::Qty{25000000},
    };
}

te::DecodedEventRecord makeTradeRecord() {
    return te::TradeEvent{
        .venue_timestamp_us = 102,
        .buy_order_id = te::OrderId{42},
        .sell_order_id = te::OrderId{99},
        .quantity = te::Qty{25000000},
    };
}

void writeSegment(const std::filesystem::path& path) {
    auto opened = te::EventSegmentWriter::open(path, makeHeader());
    ASSERT_TRUE(opened.hasValue());
    ASSERT_TRUE(opened.valueIf()->append(makeOrderRecord()).hasValue());
    ASSERT_TRUE(opened.valueIf()->append(makeTradeRecord()).hasValue());
    ASSERT_TRUE(opened.valueIf()->finish().hasValue());
}

}  // namespace

TEST(EventSegmentReaderOpen, MissingFileReturnsCannotOpen) {
    const TempReaderDirectory directory{"te_event_segment_reader_missing"};

    const auto opened = te::EventSegmentReader::open(directory.path() / "missing.bin");

    ASSERT_FALSE(opened.hasValue());
    ASSERT_NE(opened.errorIf(), nullptr);
    EXPECT_EQ(*opened.errorIf(), te::SegmentReaderError::cannot_open);
}

TEST(EventSegmentReaderOpen, TruncatedHeaderIsRejected) {
    const TempReaderDirectory directory{"te_event_segment_reader_short_header"};
    const auto path = directory.path() / "segment.bin";
    {
        std::ofstream output{path, std::ios::binary};
        const std::array<std::byte, 10> shortHeader{};
        output.write(reinterpret_cast<const char*>(shortHeader.data()),
                     static_cast<std::streamsize>(shortHeader.size()));
    }

    const auto opened = te::EventSegmentReader::open(path);

    ASSERT_FALSE(opened.hasValue());
    ASSERT_NE(opened.errorIf(), nullptr);
    EXPECT_EQ(*opened.errorIf(), te::SegmentReaderError::header_read_failed);
}

TEST(EventSegmentReaderOpen, InvalidHeaderIsRejected) {
    const TempReaderDirectory directory{"te_event_segment_reader_bad_header"};
    const auto path = directory.path() / "segment.bin";
    {
        std::ofstream output{path, std::ios::binary};
        const std::array<std::byte, te::kSegmentHeaderSize> invalidHeader{};
        output.write(reinterpret_cast<const char*>(invalidHeader.data()),
                     static_cast<std::streamsize>(invalidHeader.size()));
    }

    const auto opened = te::EventSegmentReader::open(path);

    ASSERT_FALSE(opened.hasValue());
    ASSERT_NE(opened.errorIf(), nullptr);
    EXPECT_EQ(*opened.errorIf(), te::SegmentReaderError::header_decode_failed);
}

TEST(EventSegmentReaderOpen, ValidHeaderIsExposedAndCounted) {
    const TempReaderDirectory directory{"te_event_segment_reader_valid_header"};
    const auto path = directory.path() / "segment.bin";
    auto writer = te::EventSegmentWriter::open(path, makeHeader());
    ASSERT_TRUE(writer.hasValue());
    ASSERT_TRUE(writer.valueIf()->finish().hasValue());

    const auto opened = te::EventSegmentReader::open(path);

    ASSERT_TRUE(opened.hasValue());
    EXPECT_EQ(opened.valueIf()->header().recordSize, te::kEventRecordSize);
    EXPECT_EQ(opened.valueIf()->header().seedTimestampMicros, 100U);
    EXPECT_EQ(opened.valueIf()->stats().recordsRead, 0U);
    EXPECT_EQ(opened.valueIf()->stats().bytesRead, te::kSegmentHeaderSize);
    EXPECT_FALSE(opened.valueIf()->isFinished());
}

TEST(EventSegmentReaderNext, MixedRecordsRoundTripInOriginalOrder) {
    const TempReaderDirectory directory{"te_event_segment_reader_roundtrip"};
    const auto path = directory.path() / "segment.bin";
    writeSegment(path);
    auto opened = te::EventSegmentReader::open(path);
    ASSERT_TRUE(opened.hasValue());

    const auto first = opened.valueIf()->next();
    const auto second = opened.valueIf()->next();
    const auto end = opened.valueIf()->next();

    ASSERT_TRUE(first.hasValue());
    ASSERT_TRUE(first.valueIf()->has_value());
    ASSERT_TRUE(second.hasValue());
    ASSERT_TRUE(second.valueIf()->has_value());
    ASSERT_TRUE(end.hasValue());
    EXPECT_FALSE(end.valueIf()->has_value());
    EXPECT_TRUE(std::holds_alternative<te::DecodedOrderRecord>(
        **first.valueIf()));
    EXPECT_TRUE(std::holds_alternative<te::TradeEvent>(**second.valueIf()));
    EXPECT_EQ(std::get<te::DecodedOrderRecord>(**first.valueIf()).event.order_id,
              te::OrderId{42});
    EXPECT_EQ(std::get<te::TradeEvent>(**second.valueIf()).sell_order_id,
              te::OrderId{99});
    EXPECT_EQ(opened.valueIf()->stats().recordsRead, 2U);
    EXPECT_EQ(opened.valueIf()->stats().bytesRead,
              te::kSegmentHeaderSize + (2U * te::kEventRecordSize));
    EXPECT_TRUE(opened.valueIf()->isFinished());
}

TEST(EventSegmentReaderNext, TruncatedRecordIsRejectedWithoutCountingIt) {
    const TempReaderDirectory directory{"te_event_segment_reader_short_record"};
    const auto path = directory.path() / "segment.bin";
    writeSegment(path);
    std::filesystem::resize_file(
        path, te::kSegmentHeaderSize + te::kEventRecordSize + 10U);
    auto opened = te::EventSegmentReader::open(path);
    ASSERT_TRUE(opened.hasValue());
    ASSERT_TRUE(opened.valueIf()->next().hasValue());

    const auto truncated = opened.valueIf()->next();

    ASSERT_FALSE(truncated.hasValue());
    ASSERT_NE(truncated.errorIf(), nullptr);
    EXPECT_EQ(*truncated.errorIf(), te::SegmentReaderError::truncated_record);
    EXPECT_EQ(opened.valueIf()->stats().recordsRead, 1U);
    EXPECT_EQ(opened.valueIf()->stats().bytesRead,
              te::kSegmentHeaderSize + te::kEventRecordSize);
    EXPECT_TRUE(opened.valueIf()->isFinished());
}

TEST(EventSegmentReaderNext, InvalidRecordIsRejectedAndReaderBecomesTerminal) {
    const TempReaderDirectory directory{"te_event_segment_reader_invalid_record"};
    const auto path = directory.path() / "segment.bin";
    writeSegment(path);
    {
        std::fstream file{path, std::ios::binary | std::ios::in | std::ios::out};
        ASSERT_TRUE(file.is_open());
        file.seekp(static_cast<std::streamoff>(te::kSegmentHeaderSize));
        const char invalidType = static_cast<char>(0x7F);
        file.write(&invalidType, 1);
    }
    auto opened = te::EventSegmentReader::open(path);
    ASSERT_TRUE(opened.hasValue());

    const auto invalid = opened.valueIf()->next();
    const auto afterFailure = opened.valueIf()->next();

    ASSERT_FALSE(invalid.hasValue());
    ASSERT_NE(invalid.errorIf(), nullptr);
    EXPECT_EQ(*invalid.errorIf(), te::SegmentReaderError::record_decode_failed);
    ASSERT_FALSE(afterFailure.hasValue());
    ASSERT_NE(afterFailure.errorIf(), nullptr);
    EXPECT_EQ(*afterFailure.errorIf(), te::SegmentReaderError::reader_finished);
    EXPECT_EQ(opened.valueIf()->stats().recordsRead, 0U);
    EXPECT_EQ(opened.valueIf()->stats().bytesRead, te::kSegmentHeaderSize);
}

TEST(EventSegmentReaderNext, CallingNextAfterCleanEndReturnsReaderFinished) {
    const TempReaderDirectory directory{"te_event_segment_reader_finished"};
    const auto path = directory.path() / "segment.bin";
    auto writer = te::EventSegmentWriter::open(path, makeHeader());
    ASSERT_TRUE(writer.hasValue());
    ASSERT_TRUE(writer.valueIf()->finish().hasValue());
    auto opened = te::EventSegmentReader::open(path);
    ASSERT_TRUE(opened.hasValue());
    const auto end = opened.valueIf()->next();
    ASSERT_TRUE(end.hasValue());
    ASSERT_FALSE(end.valueIf()->has_value());

    const auto afterEnd = opened.valueIf()->next();

    ASSERT_FALSE(afterEnd.hasValue());
    ASSERT_NE(afterEnd.errorIf(), nullptr);
    EXPECT_EQ(*afterEnd.errorIf(), te::SegmentReaderError::reader_finished);
}
