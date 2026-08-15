#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <te/telemetry/record.hpp>
#include <te/telemetry/recorder.hpp>
#include <te/telemetry/sink.hpp>

namespace {

/// Removes its path on destruction so a failing assertion cannot leave a stray file behind.
class TempFile {
public:
    explicit TempFile(const std::string& name)
        : path_{(std::filesystem::temp_directory_path() / name).string()} {
        std::filesystem::remove(path_);
    }
    ~TempFile() { std::filesystem::remove(path_); }
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

te::InstrumentSpec btcUsd() {
    return te::InstrumentSpec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
}

te::Clock fixedClock(std::int64_t nanos) {
    te::Clock clock;
    clock.now = [nanos]() { return te::Nanos{nanos}; };
    return clock;
}

std::vector<te::Record> readAllRecords(const std::string& path) {
    std::vector<te::Record> records;
    std::ifstream in(path, std::ios::binary);
    te::Record record{};
    while (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {
        records.push_back(record);
    }
    return records;
}

// One real captured order_deleted line.
constexpr const char* kOrderLine =
    R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269861947000","amount":0.00171371,"amount_str":"0.00171371","amount_traded":"0","amount_at_create":"0.00171371","price":58356.1,"price_str":"58356.10","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-2678-0000-000101000020","pre_event_id":"0006589a-5c97-f798-0000-000100000020","order_source":"orderbook"})";

// The subscription confirmation every capture begins with. Valid, but not an order.
constexpr const char* kSubscriptionLine =
    R"({"event":"bts:subscription_succeeded","channel":"live_orders_btcusd","data":{}})";

}  // namespace

TEST(Recorder, DecodesOrderLineAndWritesOneRecord) {
    const TempFile out("te_recorder_one.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input(std::string(kOrderLine) + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(42));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->linesRead, 1U);
    EXPECT_EQ(result.valueIf()->written, 1U);
    EXPECT_EQ(result.valueIf()->skipped, 0U);
    EXPECT_EQ(result.valueIf()->failed, 0U);
}

// The whole point of the skipped/failed split: a protocol message is not an error.
TEST(Recorder, CountsSubscriptionMessageAsSkippedNotFailed) {
    const TempFile out("te_recorder_skip.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input(std::string(kSubscriptionLine) + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(1));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->skipped, 1U);
    EXPECT_EQ(result.valueIf()->failed, 0U);
    EXPECT_EQ(result.valueIf()->written, 0U);
}

TEST(Recorder, CountsUndecodableLineAsFailed) {
    const TempFile out("te_recorder_fail.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input("this is not json\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(1));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->failed, 1U);
    EXPECT_EQ(result.valueIf()->written, 0U);
    EXPECT_EQ(result.valueIf()->skipped, 0U);
}

TEST(Recorder, BlankLinesAreSkippedNotFailed) {
    const TempFile out("te_recorder_blank.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input("\n\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(1));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->linesRead, 2U);
    EXPECT_EQ(result.valueIf()->skipped, 2U);
    EXPECT_EQ(result.valueIf()->failed, 0U);
}

TEST(Recorder, EmptyInputProducesEmptyCapture) {
    const TempFile out("te_recorder_empty.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input("");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(1));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->linesRead, 0U);
    EXPECT_EQ(result.valueIf()->written, 0U);
}

// Records must land in input order, since replay depends on it.
TEST(Recorder, PreservesInputOrder) {
    const TempFile out("te_recorder_order.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input(std::string(kSubscriptionLine) + "\n" + kOrderLine + "\n" +
                             kOrderLine + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(7));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->linesRead, 3U);
    EXPECT_EQ(result.valueIf()->written, 2U);
    EXPECT_EQ(result.valueIf()->skipped, 1U);

    const auto records = readAllRecords(out.path());
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].orderEvent.order_id, te::OrderId{2037493297635328ULL});
    EXPECT_EQ(records[0].orderEvent.price, te::Price{5835610});
    EXPECT_EQ(records[1].orderEvent.order_id, te::OrderId{2037493297635328ULL});
}

// The invariant the loop asserts: every line lands in exactly one bucket.
TEST(Recorder, CountersAccountForEveryLine) {
    const TempFile out("te_recorder_invariant.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input(std::string(kOrderLine) + "\n" + kSubscriptionLine + "\n" +
                             "not json\n" + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(3));

    ASSERT_TRUE(result.hasValue());
    const te::RecorderStats& s = *result.valueIf();
    EXPECT_EQ(s.linesRead, s.written + s.skipped + s.failed);
    EXPECT_EQ(s.linesRead, 4U);
    EXPECT_EQ(s.written, 1U);
    EXPECT_EQ(s.skipped, 2U);  // subscription + blank
    EXPECT_EQ(s.failed, 1U);
}

// The golden test the plan called for: the real capture, end to end, byte-exact.
TEST(Recorder, GoldenCaptureRoundTripsByteExact) {
    const std::string fixture =
        std::string(TE_PROJECT_ROOT_DIR) +
        "/data/raw/bitstamp-btcusd-20260809T100421Z/segment-0000.jsonl";

    if (!std::filesystem::exists(fixture)) {
        GTEST_SKIP() << "capture not present: " << fixture;
    }

    const TempFile out("te_recorder_golden.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::ifstream input(fixture);
    ASSERT_TRUE(input.is_open());

    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(99));
    ASSERT_TRUE(result.hasValue());

    const te::RecorderStats& s = *result.valueIf();
    EXPECT_EQ(s.linesRead, 1434U);
    EXPECT_EQ(s.written, 1433U);
    EXPECT_EQ(s.skipped, 1U);   // bts:subscription_succeeded
    EXPECT_EQ(s.failed, 0U);

    // Fixed size is what makes "seek to record N" arithmetic rather than a scan.
    EXPECT_EQ(std::filesystem::file_size(out.path()), sizeof(te::Record) * 1433);

    const auto records = readAllRecords(out.path());
    ASSERT_EQ(records.size(), 1433U);

    // First record matches the first order line in the capture.
    EXPECT_EQ(records[0].orderEvent.order_id, te::OrderId{2037493297635328ULL});
    EXPECT_EQ(records[0].orderEvent.price, te::Price{5835610});
    EXPECT_EQ(records[0].orderEvent.quantity, te::Qty{171371});
    EXPECT_EQ(records[0].orderEvent.side, te::Side::buy);
    EXPECT_EQ(records[0].orderEvent.kind, te::EventKind::remove);
    EXPECT_EQ(records[0].version, te::kCurrentRecordVersion);

    // Padding must be zero, or two builds produce byte-different captures for identical input.
    for (const te::Record& record : records) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&record);
        for (std::size_t i = 34; i < 40; ++i) {
            ASSERT_EQ(bytes[i], 0U) << "non-zero padding at byte " << i;
        }
        for (std::size_t i = 41; i < 48; ++i) {
            ASSERT_EQ(bytes[i], 0U) << "non-zero padding at byte " << i;
        }
    }
}
