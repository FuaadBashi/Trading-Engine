#include <gtest/gtest.h>

#include <array>
#include <csignal>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <sys/resource.h>
#include <system_error>
#include <type_traits>
#include <variant>
#include <vector>

#include "te/telemetry/event_segment_writer.hpp"

namespace {

class TempWriterDirectory {
public:
    explicit TempWriterDirectory(std::string_view name)
        : path_{std::filesystem::temp_directory_path() / name} {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempWriterDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

te::SegmentHeader makeEventHeader() {
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

std::vector<std::byte> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input.is_open()) {
        return {};
    }

    const auto end = input.tellg();
    if (end < 0) {
        return {};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        return {};
    }
    return bytes;
}

// Forces real write() calls against the backing file to fail with EFBIG instead of silently
// buffering, by capping how large the process is allowed to grow any file. SIGXFSZ is ignored for
// the guard's lifetime so the failing write reports an error instead of killing the test process.
class FileSizeLimitGuard {
public:
    explicit FileSizeLimitGuard(rlim_t maxBytes) {
        ::getrlimit(RLIMIT_FSIZE, &previousLimit_);
        previousHandler_ = std::signal(SIGXFSZ, SIG_IGN);
        struct rlimit capped = previousLimit_;
        capped.rlim_cur = maxBytes;
        ::setrlimit(RLIMIT_FSIZE, &capped);
    }

    ~FileSizeLimitGuard() {
        ::setrlimit(RLIMIT_FSIZE, &previousLimit_);
        std::signal(SIGXFSZ, previousHandler_);
    }

    FileSizeLimitGuard(const FileSizeLimitGuard&) = delete;
    FileSizeLimitGuard& operator=(const FileSizeLimitGuard&) = delete;

private:
    struct rlimit previousLimit_ {};
    void (*previousHandler_)(int) {};
};

}  // namespace

TEST(EventSegmentWriterInterface, HasSingleMovableOwnershipOfItsFile) {
    EXPECT_FALSE(std::is_default_constructible_v<te::EventSegmentWriter>);
    EXPECT_FALSE(std::is_copy_constructible_v<te::EventSegmentWriter>);
    EXPECT_FALSE(std::is_copy_assignable_v<te::EventSegmentWriter>);
    EXPECT_TRUE(std::is_move_constructible_v<te::EventSegmentWriter>);
    EXPECT_TRUE(std::is_move_assignable_v<te::EventSegmentWriter>);
}

TEST(EventSegmentWriterInterface, StatsBeginAtZero) {
    const te::SegmentWriterStats stats;

    EXPECT_EQ(stats.recordsWritten, 0U);
    EXPECT_EQ(stats.bytesWritten, 0U);
}

TEST(EventSegmentWriterOpen, ValidHeaderCreatesExactlyOneHeader) {
    const TempWriterDirectory directory{"te_event_segment_writer_valid_header"};
    const auto outputPath = directory.path() / "segment-0000.bin";

    const auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());

    ASSERT_TRUE(opened.hasValue());
    EXPECT_TRUE(std::filesystem::exists(outputPath));
    EXPECT_EQ(std::filesystem::file_size(outputPath), te::kSegmentHeaderSize);
}

TEST(EventSegmentWriterOpen, WrittenFileBeginsWithSegmentMagic) {
    const TempWriterDirectory directory{"te_event_segment_writer_magic"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    ASSERT_TRUE(te::EventSegmentWriter::open(outputPath, makeEventHeader()).hasValue());

    std::ifstream input{outputPath, std::ios::binary};
    ASSERT_TRUE(input.is_open());
    std::array<std::byte, te::kSegmentMagic.size()> magic{};
    input.read(reinterpret_cast<char*>(magic.data()),
               static_cast<std::streamsize>(magic.size()));

    ASSERT_TRUE(input.good());
    EXPECT_EQ(magic, te::kSegmentMagic);
}

TEST(EventSegmentWriterOpen, InitialStatsCountOnlyTheHeader) {
    const TempWriterDirectory directory{"te_event_segment_writer_initial_stats"};
    const auto outputPath = directory.path() / "segment-0000.bin";

    const auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());

    ASSERT_TRUE(opened.hasValue());
    EXPECT_EQ(opened.valueIf()->stats().recordsWritten, 0U);
    EXPECT_EQ(opened.valueIf()->stats().bytesWritten, te::kSegmentHeaderSize);
}

TEST(EventSegmentWriterOpen, ExistingOutputIsNotOverwritten) {
    const TempWriterDirectory directory{"te_event_segment_writer_existing_output"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    {
        std::ofstream existing{outputPath, std::ios::binary};
        ASSERT_TRUE(existing.is_open());
        existing << "keep";
    }

    const auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());

    ASSERT_FALSE(opened.hasValue());
    ASSERT_NE(opened.errorIf(), nullptr);
    EXPECT_EQ(*opened.errorIf(), te::SegmentWriterError::output_already_exists);
    EXPECT_EQ(std::filesystem::file_size(outputPath), 4U);
}

TEST(EventSegmentWriterOpen, InvalidHeaderDoesNotCreateOutput) {
    const TempWriterDirectory directory{"te_event_segment_writer_invalid_header"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    te::SegmentHeader header = makeEventHeader();
    header.recordSize = 7;

    const auto opened = te::EventSegmentWriter::open(outputPath, header);

    ASSERT_FALSE(opened.hasValue());
    ASSERT_NE(opened.errorIf(), nullptr);
    EXPECT_EQ(*opened.errorIf(), te::SegmentWriterError::header_encode_failed);
    EXPECT_FALSE(std::filesystem::exists(outputPath));
}

TEST(EventSegmentWriterAppend, ValidOrderUpdatesAccounting) {
    const TempWriterDirectory directory{"te_event_segment_writer_append_order"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());
    ASSERT_TRUE(opened.hasValue());

    const auto appended = opened.valueIf()->append(makeOrderRecord());

    ASSERT_TRUE(appended.hasValue());
    EXPECT_EQ(*appended.valueIf(), te::kEventRecordSize);
    EXPECT_EQ(opened.valueIf()->stats().recordsWritten, 1U);
    EXPECT_EQ(opened.valueIf()->stats().bytesWritten,
              te::kSegmentHeaderSize + te::kEventRecordSize);
}

TEST(EventSegmentWriterAppend, InvalidRecordDoesNotChangeAccounting) {
    const TempWriterDirectory directory{"te_event_segment_writer_invalid_record"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());
    ASSERT_TRUE(opened.hasValue());
    te::DecodedOrderRecord invalid = std::get<te::DecodedOrderRecord>(makeOrderRecord());
    invalid.event.side = static_cast<te::Side>(99);

    const auto appended = opened.valueIf()->append(te::DecodedEventRecord{invalid});

    ASSERT_FALSE(appended.hasValue());
    ASSERT_NE(appended.errorIf(), nullptr);
    EXPECT_EQ(*appended.errorIf(), te::SegmentWriterError::record_encode_failed);
    EXPECT_EQ(opened.valueIf()->stats().recordsWritten, 0U);
    EXPECT_EQ(opened.valueIf()->stats().bytesWritten, te::kSegmentHeaderSize);
}

TEST(EventSegmentWriterAppend, WriteFailureMakesWriterTerminal) {
    const TempWriterDirectory directory{"te_event_segment_writer_write_failure"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());
    ASSERT_TRUE(opened.hasValue());

    // Leaves room for the header plus a couple of records; ofstream buffers internally, so the
    // OS-level write that actually crosses this ceiling may not be the very first append() call.
    const FileSizeLimitGuard sizeLimit{
        static_cast<rlim_t>(te::kSegmentHeaderSize + (2U * te::kEventRecordSize))};

    std::optional<te::SegmentWriterError> firstFailure;
    for (int attempt = 0; attempt < 300 && !firstFailure.has_value(); ++attempt) {
        const auto appended = opened.valueIf()->append(makeOrderRecord());
        if (!appended.hasValue()) {
            firstFailure = *appended.errorIf();
        }
    }

    ASSERT_TRUE(firstFailure.has_value());
    EXPECT_EQ(*firstFailure, te::SegmentWriterError::record_write_failed);
    EXPECT_TRUE(opened.valueIf()->isFinished());

    const auto appendAfterFailure = opened.valueIf()->append(makeOrderRecord());
    const auto finishAfterFailure = opened.valueIf()->finish();

    ASSERT_FALSE(appendAfterFailure.hasValue());
    ASSERT_NE(appendAfterFailure.errorIf(), nullptr);
    EXPECT_EQ(*appendAfterFailure.errorIf(), te::SegmentWriterError::writer_finished);
    ASSERT_FALSE(finishAfterFailure.hasValue());
    ASSERT_NE(finishAfterFailure.errorIf(), nullptr);
    EXPECT_EQ(*finishAfterFailure.errorIf(), te::SegmentWriterError::writer_finished);
}

TEST(EventSegmentWriterFinish, EmptySegmentClosesWithHeaderOnly) {
    const TempWriterDirectory directory{"te_event_segment_writer_empty_finish"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());
    ASSERT_TRUE(opened.hasValue());

    const auto finished = opened.valueIf()->finish();

    ASSERT_TRUE(finished.hasValue());
    EXPECT_TRUE(opened.valueIf()->isFinished());
    EXPECT_EQ(finished.valueIf()->recordsWritten, 0U);
    EXPECT_EQ(finished.valueIf()->bytesWritten, te::kSegmentHeaderSize);
    EXPECT_EQ(std::filesystem::file_size(outputPath), te::kSegmentHeaderSize);
}

TEST(EventSegmentWriterFinish, MixedRecordsRemainInAppendOrderAndRoundTrip) {
    const TempWriterDirectory directory{"te_event_segment_writer_mixed_records"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());
    ASSERT_TRUE(opened.hasValue());
    ASSERT_TRUE(opened.valueIf()->append(makeOrderRecord()).hasValue());
    ASSERT_TRUE(opened.valueIf()->append(makeTradeRecord()).hasValue());

    const auto finished = opened.valueIf()->finish();

    ASSERT_TRUE(finished.hasValue());
    EXPECT_EQ(finished.valueIf()->recordsWritten, 2U);
    EXPECT_EQ(finished.valueIf()->bytesWritten,
              te::kSegmentHeaderSize + (2U * te::kEventRecordSize));

    const std::vector<std::byte> bytes = readBinaryFile(outputPath);
    ASSERT_EQ(bytes.size(), finished.valueIf()->bytesWritten);
    const auto order = te::decodeEventRecord(std::span<const std::byte>{
        bytes.data() + te::kSegmentHeaderSize, te::kEventRecordSize});
    const auto trade = te::decodeEventRecord(std::span<const std::byte>{
        bytes.data() + te::kSegmentHeaderSize + te::kEventRecordSize,
        te::kEventRecordSize});

    ASSERT_TRUE(order.hasValue());
    ASSERT_TRUE(trade.hasValue());
    EXPECT_TRUE(std::holds_alternative<te::DecodedOrderRecord>(*order.valueIf()));
    EXPECT_TRUE(std::holds_alternative<te::TradeEvent>(*trade.valueIf()));
    EXPECT_EQ(std::get<te::DecodedOrderRecord>(*order.valueIf()).event.order_id,
              te::OrderId{42});
    EXPECT_EQ(std::get<te::TradeEvent>(*trade.valueIf()).sell_order_id,
              te::OrderId{99});
}

TEST(EventSegmentWriterFinish, FinishedWriterRejectsFurtherOperations) {
    const TempWriterDirectory directory{"te_event_segment_writer_finished_state"};
    const auto outputPath = directory.path() / "segment-0000.bin";
    auto opened = te::EventSegmentWriter::open(outputPath, makeEventHeader());
    ASSERT_TRUE(opened.hasValue());
    ASSERT_TRUE(opened.valueIf()->finish().hasValue());

    const auto appended = opened.valueIf()->append(makeOrderRecord());
    const auto finishedAgain = opened.valueIf()->finish();

    ASSERT_FALSE(appended.hasValue());
    ASSERT_NE(appended.errorIf(), nullptr);
    EXPECT_EQ(*appended.errorIf(), te::SegmentWriterError::writer_finished);
    ASSERT_FALSE(finishedAgain.hasValue());
    ASSERT_NE(finishedAgain.errorIf(), nullptr);
    EXPECT_EQ(*finishedAgain.errorIf(), te::SegmentWriterError::writer_finished);
}
