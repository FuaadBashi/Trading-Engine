#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <te/telemetry/legacy/record.hpp>
#include <te/telemetry/legacy/recorder.hpp>
#include <te/telemetry/legacy/sink.hpp>

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

// The line that genuinely follows kOrderLine in the capture: its pre_event_id is kOrderLine's
// event_id, so the two form an unbroken chain.
constexpr const char* kNextOrderLine =
    R"({"data":{"id":2037493293895680,"id_str":"2037493293895680","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269862001000","amount":2.31332185,"amount_str":"2.31332185","amount_traded":"0","amount_at_create":"2.31332185","price":64839,"price_str":"64839.00","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-f968-0000-000102000020","pre_event_id":"0006589a-5c98-2678-0000-000101000020","order_source":"orderbook"})";

// A line whose pre_event_id points at a message that never arrived: the chain is broken.
constexpr const char* kChainBreakLine =
    R"({"data":{"id":2037493298597888,"id_str":"2037493298597888","order_type":1,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269862010000","amount":2.31331873,"amount_str":"2.31331873","amount_traded":"0","amount_at_create":"2.31331873","price":64839.06,"price_str":"64839.06","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_created","event_id":"0006589a-ffff-0000-0000-000199000020","pre_event_id":"0006589a-dead-beef-0000-000999000020","order_source":"orderbook"})";

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
                             kNextOrderLine + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(7));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->linesRead, 3U);
    EXPECT_EQ(result.valueIf()->written, 2U);
    EXPECT_EQ(result.valueIf()->skipped, 1U);
    EXPECT_EQ(result.valueIf()->gapsDetected, 0U);

    const auto records = readAllRecords(out.path());
    ASSERT_EQ(records.size(), 2U);
    EXPECT_EQ(records[0].orderEvent.order_id, te::OrderId{2037493297635328ULL});
    EXPECT_EQ(records[0].orderEvent.price, te::Price{5835610});
    EXPECT_EQ(records[1].orderEvent.order_id, te::OrderId{2037493293895680ULL});
    EXPECT_EQ(records[1].orderEvent.price, te::Price{6483900});
}

// ---- gap detection (ADR 0006)

TEST(Recorder, DetectsBrokenChainAndWritesGapMarker) {
    const TempFile out("te_recorder_gap.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    // Second line's pre_event_id does not name the first line, so events were lost between them.
    std::istringstream input(std::string(kOrderLine) + "\n" + kChainBreakLine + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(5));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->gapsDetected, 1U);
    EXPECT_EQ(result.valueIf()->linesRead, 2U);
    EXPECT_EQ(result.valueIf()->written, 3U);  // two events plus the marker
    EXPECT_EQ(result.valueIf()->failed, 0U);   // a gap is not a decode failure

    const auto records = readAllRecords(out.path());
    ASSERT_EQ(records.size(), 3U);

    // The marker sits between the two events, marking where continuity was lost.
    EXPECT_EQ(records[0].kind, te::RecordKind::order_event);
    EXPECT_EQ(records[1].kind, te::RecordKind::gap);
    EXPECT_EQ(records[2].kind, te::RecordKind::order_event);

    // A gap marker carries no event data, only its position and detection time.
    EXPECT_EQ(records[1].orderEvent.order_id, te::OrderId{0});
    EXPECT_EQ(records[1].orderEvent.price, te::Price{0});
    EXPECT_EQ(records[1].receipt_timestamp_ns, te::Nanos{5});
    EXPECT_EQ(records[1].version, te::kCurrentRecordVersion);
}

// An unbroken chain must never produce a marker, or every replay would be censored for nothing.
TEST(Recorder, IntactChainProducesNoGapMarker) {
    const TempFile out("te_recorder_nogap.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input(std::string(kOrderLine) + "\n" + kNextOrderLine + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(1));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->gapsDetected, 0U);
    EXPECT_EQ(result.valueIf()->written, 2U);

    for (const te::Record& record : readAllRecords(out.path())) {
        EXPECT_EQ(record.kind, te::RecordKind::order_event);
    }
}

// The first order event has no predecessor; treating that as a break would open every capture
// with a false gap.
TEST(Recorder, FirstEventCannotBreakTheChain) {
    const TempFile out("te_recorder_first.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input(std::string(kChainBreakLine) + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(1));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->gapsDetected, 0U);
    EXPECT_EQ(result.valueIf()->written, 1U);
}

// Protocol messages carry no chain ids and must not be treated as breaking continuity.
TEST(Recorder, SubscriptionMessageDoesNotBreakTheChain) {
    const TempFile out("te_recorder_proto.bin");
    auto opened = te::Sink::open(out.path());
    ASSERT_TRUE(opened.hasValue());

    std::istringstream input(std::string(kOrderLine) + "\n" + kSubscriptionLine + "\n" +
                             kNextOrderLine + "\n");
    const auto result = te::runRecorder(input, *opened.valueIf(), btcUsd(), fixedClock(1));

    ASSERT_TRUE(result.hasValue());
    EXPECT_EQ(result.valueIf()->gapsDetected, 0U);
    EXPECT_EQ(result.valueIf()->skipped, 1U);
    EXPECT_EQ(result.valueIf()->written, 2U);
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
        GTEST_SKIP() << "OPTIONAL EVIDENCE NOT RUN: the recorder is unverified against a real\n                        capture file; synthetic recorder tests still ran. Missing: " << fixture;
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
