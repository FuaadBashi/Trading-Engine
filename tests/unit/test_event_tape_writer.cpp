#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "te/capture/event_tape_writer.hpp"
#include "te/telemetry/event_segment_reader.hpp"

namespace {

class TempTapeDirectory {
public:
    explicit TempTapeDirectory(std::string_view name)
        : path_{std::filesystem::temp_directory_path() / name} {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempTapeDirectory() {
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

te::CapturedOrderEvent order(std::uint64_t timestamp, std::uint64_t id) {
    te::CapturedOrderEvent captured{};
    captured.event.venue_timestamp_us = timestamp;
    captured.event.order_id = te::OrderId{id};
    captured.event.price = te::Price{12345};
    captured.event.quantity = te::Qty{60000000};
    captured.event.side = te::Side::buy;
    captured.event.kind = te::EventKind::add;
    captured.amountTraded = te::Qty{0};
    return captured;
}

te::CapturedTradeEvent trade(std::uint64_t timestamp, std::uint64_t buyId) {
    te::CapturedTradeEvent captured{};
    captured.event.venue_timestamp_us = timestamp;
    captured.event.buy_order_id = te::OrderId{buyId};
    captured.event.sell_order_id = te::OrderId{buyId + 1};
    captured.event.quantity = te::Qty{25000000};
    return captured;
}

// Reads a tape back through the production reader, so the test proves the bytes are decodable
// rather than trusting the writer's own accounting.
std::vector<te::DecodedEventRecord> readTape(const std::filesystem::path& path) {
    std::vector<te::DecodedEventRecord> records;
    auto opened = te::EventSegmentReader::open(path);
    if (!opened.hasValue()) {
        return records;
    }
    while (true) {
        auto next = opened.valueIf()->next();
        if (!next.hasValue() || !next.valueIf()->has_value()) {
            break;
        }
        records.push_back(**next.valueIf());
    }
    return records;
}

}  // namespace

TEST(EventTapeWriter, MergedOrderSurvivesTheRoundTrip) {
    const TempTapeDirectory directory{"te_event_tape_roundtrip"};
    const auto path = directory.path() / "tape.bin";
    te::JoinedCapture capture;
    capture.jc_captureOrderEvents = {order(10, 1), order(30, 2)};
    capture.jc_tradeEvents = {trade(20, 3)};

    const auto written = te::writeEventTape(capture, makeHeader(), 0, UINT64_MAX, path);

    ASSERT_TRUE(written.hasValue());
    EXPECT_EQ(written.valueIf()->ordersWritten, 2U);
    EXPECT_EQ(written.valueIf()->tradesWritten, 1U);

    const auto records = readTape(path);
    ASSERT_EQ(records.size(), 3U);
    EXPECT_TRUE(std::holds_alternative<te::DecodedOrderRecord>(records[0]));
    EXPECT_TRUE(std::holds_alternative<te::TradeEvent>(records[1]));
    EXPECT_TRUE(std::holds_alternative<te::DecodedOrderRecord>(records[2]));
    EXPECT_EQ(std::get<te::DecodedOrderRecord>(records[0]).event.order_id, te::OrderId{1});
    EXPECT_EQ(std::get<te::TradeEvent>(records[1]).buy_order_id, te::OrderId{3});
    EXPECT_EQ(std::get<te::DecodedOrderRecord>(records[2]).event.order_id, te::OrderId{2});
}

TEST(EventTapeWriter, ExactTieWritesTheOrderEventFirst) {
    const TempTapeDirectory directory{"te_event_tape_tie"};
    const auto path = directory.path() / "tape.bin";
    te::JoinedCapture capture;
    capture.jc_captureOrderEvents = {order(10, 1)};
    capture.jc_tradeEvents = {trade(10, 3)};

    ASSERT_TRUE(te::writeEventTape(capture, makeHeader(), 0, UINT64_MAX, path).hasValue());

    const auto records = readTape(path);
    ASSERT_EQ(records.size(), 2U);
    EXPECT_TRUE(std::holds_alternative<te::DecodedOrderRecord>(records[0]));
    EXPECT_TRUE(std::holds_alternative<te::TradeEvent>(records[1]));
}

TEST(EventTapeWriter, StampsTheOrderingPolicyInTheHeader) {
    const TempTapeDirectory directory{"te_event_tape_policy"};
    const auto path = directory.path() / "tape.bin";
    te::JoinedCapture capture;
    capture.jc_captureOrderEvents = {order(10, 1)};

    ASSERT_TRUE(te::writeEventTape(capture, makeHeader(), 0, UINT64_MAX, path).hasValue());

    const auto opened = te::EventSegmentReader::open(path);
    ASSERT_TRUE(opened.hasValue());
    EXPECT_EQ(opened.valueIf()->header().orderingPolicyVersion, te::kOrderingPolicyOrderWinsTie);
}

TEST(EventTapeWriter, AmountTradedReachesTheTape) {
    const TempTapeDirectory directory{"te_event_tape_fill"};
    const auto path = directory.path() / "tape.bin";
    te::JoinedCapture capture;
    te::CapturedOrderEvent filled = order(10, 1);
    filled.amountTraded = te::Qty{25000000};
    capture.jc_captureOrderEvents = {filled};

    ASSERT_TRUE(te::writeEventTape(capture, makeHeader(), 0, UINT64_MAX, path).hasValue());

    const auto records = readTape(path);
    ASSERT_EQ(records.size(), 1U);
    EXPECT_EQ(std::get<te::DecodedOrderRecord>(records[0]).amountTraded, te::Qty{25000000});
}

TEST(EventTapeWriter, WindowBoundsExcludeSeedAndCutoff) {
    const TempTapeDirectory directory{"te_event_tape_window"};
    const auto path = directory.path() / "tape.bin";
    te::JoinedCapture capture;
    capture.jc_captureOrderEvents = {order(5, 1), order(15, 2), order(35, 3)};
    capture.jc_tradeEvents = {trade(5, 4), trade(45, 5)};

    const auto written = te::writeEventTape(capture, makeHeader(), 10, 30, path);

    ASSERT_TRUE(written.hasValue());
    EXPECT_EQ(written.valueIf()->ordersBeforeSeed, 1U);
    EXPECT_EQ(written.valueIf()->ordersWritten, 1U);
    EXPECT_EQ(written.valueIf()->ordersAfterCutoff, 1U);
    EXPECT_EQ(written.valueIf()->tradesBeforeSeed, 1U);
    EXPECT_EQ(written.valueIf()->tradesWritten, 0U);
    EXPECT_EQ(written.valueIf()->tradesAfterCutoff, 1U);
    EXPECT_EQ(readTape(path).size(), 1U);
}

// Plan v4 §12's accounting discipline, applied to the tape writer.
TEST(EventTapeWriter, EveryInputIsAccountedForExactlyOnce) {
    const TempTapeDirectory directory{"te_event_tape_accounting"};
    const auto path = directory.path() / "tape.bin";
    te::JoinedCapture capture;
    capture.jc_captureOrderEvents = {order(5, 1), order(15, 2), order(25, 3), order(35, 4)};
    capture.jc_tradeEvents = {trade(5, 5), trade(20, 6), trade(45, 7)};

    const auto written = te::writeEventTape(capture, makeHeader(), 10, 30, path);

    ASSERT_TRUE(written.hasValue());
    EXPECT_EQ(written.valueIf()->ordersAccountedFor(), capture.jc_captureOrderEvents.size());
    EXPECT_EQ(written.valueIf()->tradesAccountedFor(), capture.jc_tradeEvents.size());
}

TEST(EventTapeWriter, EmptyWindowStillWritesAValidHeaderOnlyTape) {
    const TempTapeDirectory directory{"te_event_tape_empty"};
    const auto path = directory.path() / "tape.bin";
    te::JoinedCapture capture;
    capture.jc_captureOrderEvents = {order(5, 1)};

    const auto written = te::writeEventTape(capture, makeHeader(), 10, UINT64_MAX, path);

    ASSERT_TRUE(written.hasValue());
    EXPECT_EQ(written.valueIf()->bytesWritten, te::kSegmentHeaderSize);
    EXPECT_EQ(std::filesystem::file_size(path), te::kSegmentHeaderSize);
    EXPECT_TRUE(te::EventSegmentReader::open(path).hasValue());
}

TEST(EventTapeWriter, RejectsAHeaderThatIsNotEventFormat) {
    const TempTapeDirectory directory{"te_event_tape_bad_header"};
    const auto path = directory.path() / "tape.bin";
    te::SegmentHeader header = makeHeader();
    header.recordSize = te::kSnapshotRecordSize;

    const auto written = te::writeEventTape(te::JoinedCapture{}, header, 0, UINT64_MAX, path);

    ASSERT_FALSE(written.hasValue());
    ASSERT_NE(written.errorIf(), nullptr);
    EXPECT_EQ(*written.errorIf(), te::TapeWriteError::header_not_event_format);
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(EventTapeWriter, RefusesToOverwriteAnExistingTape) {
    const TempTapeDirectory directory{"te_event_tape_existing"};
    const auto path = directory.path() / "tape.bin";
    ASSERT_TRUE(te::writeEventTape(te::JoinedCapture{}, makeHeader(), 0, UINT64_MAX, path)
                    .hasValue());

    const auto second = te::writeEventTape(te::JoinedCapture{}, makeHeader(), 0, UINT64_MAX, path);

    ASSERT_FALSE(second.hasValue());
    ASSERT_NE(second.errorIf(), nullptr);
    EXPECT_EQ(*second.errorIf(), te::TapeWriteError::output_open_failed);
}

// Regenerating a tape from the same capture must produce identical bytes, or "derived and
// regenerable" is not a property the corpus actually has.
TEST(EventTapeWriter, RegeneratingProducesIdenticalBytes) {
    const TempTapeDirectory directory{"te_event_tape_deterministic"};
    te::JoinedCapture capture;
    capture.jc_captureOrderEvents = {order(10, 1), order(30, 2)};
    capture.jc_tradeEvents = {trade(20, 3)};

    const auto first = directory.path() / "first.bin";
    const auto second = directory.path() / "second.bin";
    ASSERT_TRUE(te::writeEventTape(capture, makeHeader(), 0, UINT64_MAX, first).hasValue());
    ASSERT_TRUE(te::writeEventTape(capture, makeHeader(), 0, UINT64_MAX, second).hasValue());

    std::ifstream firstFile{first, std::ios::binary};
    std::ifstream secondFile{second, std::ios::binary};
    const std::string firstBytes{std::istreambuf_iterator<char>{firstFile},
                                 std::istreambuf_iterator<char>{}};
    const std::string secondBytes{std::istreambuf_iterator<char>{secondFile},
                                  std::istreambuf_iterator<char>{}};
    EXPECT_FALSE(firstBytes.empty());
    EXPECT_EQ(firstBytes, secondBytes);
}
