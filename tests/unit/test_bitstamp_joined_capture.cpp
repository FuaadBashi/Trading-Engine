#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <te/feed/bitstamp/joined_capture.hpp>
#include <te/feed/bitstamp/replay.hpp>

namespace {

class TempCaptureDirectory {
public:
    explicit TempCaptureDirectory(const std::string& name)
        : path_{std::filesystem::temp_directory_path() / name} {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempCaptureDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempCaptureDirectory(const TempCaptureDirectory&) = delete;
    TempCaptureDirectory& operator=(const TempCaptureDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

te::InstrumentSpec btcUsd() {
    return te::InstrumentSpec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
}

void writeTextFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream output{path, std::ios::binary};
    ASSERT_TRUE(output.is_open()) << "could not create test file: " << path;
    output << contents;
    ASSERT_TRUE(output.good()) << "could not write test file: " << path;
}

constexpr std::string_view kManifest = R"({
  "segments": [
    {
      "payload": "payload.jsonl",
      "frame_index": "frames.jsonl",
      "snapshot": "seed.snapshot",
      "checkpoint": "checkpoint.snapshot"
    }
  ]
})";

constexpr std::string_view kSeed =
    R"({"timestamp":"1","microtimestamp":"1000","bids":[],"asks":[]})";

constexpr std::string_view kCheckpoint =
    R"({"timestamp":"2","microtimestamp":"2000","bids":[],"asks":[]})";

constexpr std::string_view kOrderPayload =
    R"({"data":{"id":2037493297635328,"id_str":"2037493297635328","order_type":0,"order_subtype":5,"datetime":"1786269862","microtimestamp":"1786269861947000","amount":0.00171371,"amount_str":"0.00171371","amount_traded":"0","amount_at_create":"0.00171371","price":58356.1,"price_str":"58356.10","is_liquidation":false},"channel":"live_orders_btcusd","event":"order_deleted","event_id":"0006589a-5c98-2678-0000-000101000020","pre_event_id":"0006589a-5c97-f798-0000-000100000020","order_source":"orderbook"})";

constexpr std::string_view kTradePayload =
    R"({"data":{"id":619501668,"timestamp":"1787174919","amount":0.34778488,"amount_str":"0.34778488","price":69590.13,"price_str":"69590.13","type":0,"microtimestamp":"1787174919977000","buy_order_id":2041200416022642,"sell_order_id":2041200408317957},"channel":"live_trades_btcusd","event":"trade"})";

void writeCommonCaptureFiles(const TempCaptureDirectory& capture) {
    writeTextFile(capture.path() / "manifest.json", kManifest);
    writeTextFile(capture.path() / "seed.snapshot", kSeed);
    writeTextFile(capture.path() / "checkpoint.snapshot", kCheckpoint);
}

void expectBookMatchesSnapshot(const te::OrderBook& book,
                               const te::bitstamp::BookSnapshot& snapshot) {
    std::map<te::Price, te::Qty> expectedBids;
    std::map<te::Price, te::Qty> expectedAsks;

    for (const te::bitstamp::SnapshotOrder& order : snapshot.orders) {
        auto& expectedLevels = order.side == te::Side::buy ? expectedBids : expectedAsks;
        expectedLevels[order.price].units += order.quantity.units;
    }

    for (const auto& [price, quantity] : expectedBids) {
        EXPECT_EQ(book.qtyAt(te::Side::buy, price), quantity);
    }
    for (const auto& [price, quantity] : expectedAsks) {
        EXPECT_EQ(book.qtyAt(te::Side::sell, price), quantity);
    }

    EXPECT_EQ(book.levelCount(), expectedBids.size() + expectedAsks.size());
}

TEST(BitstampJoinedCapture, MissingPayloadFileReturnsPayloadUnreadable) {
    const TempCaptureDirectory capture{"te_joined_capture_missing_payload"};
    writeCommonCaptureFiles(capture);
    writeTextFile(capture.path() / "frames.jsonl", "");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::JoinedCaptureError::payload_unreadable);
}

TEST(BitstampJoinedCapture, MissingFrameIndexFileReturnsFrameIndexUnreadable) {
    const TempCaptureDirectory capture{"te_joined_capture_missing_frame_index"};
    writeCommonCaptureFiles(capture);
    writeTextFile(capture.path() / "payload.jsonl", "");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::JoinedCaptureError::frame_index_unreadable);
}

TEST(BitstampJoinedCapture, RejectsFrameIndexEndingEarly) {
    const TempCaptureDirectory capture{"te_joined_capture_frame_index_ended_early"};

    writeCommonCaptureFiles(capture);
    writeTextFile(capture.path() / "payload.jsonl", "{}\n");
    writeTextFile(capture.path() / "frames.jsonl", "");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::JoinedCaptureError::frame_index_ended_early);
}

TEST(BitstampJoinedCapture, RejectsPayloadEndingEarly) {
    const TempCaptureDirectory capture{"te_joined_capture_payload_ended_early"};

    writeCommonCaptureFiles(capture);
    writeTextFile(capture.path() / "payload.jsonl", "");
    writeTextFile(capture.path() / "frames.jsonl", "{}\n");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::JoinedCaptureError::payload_ended_early);
}

TEST(BitstampJoinedCapture, AcceptsBothStreamsEndingTogether) {
    const TempCaptureDirectory capture{"te_joined_capture_both_streams_end_together"};

    writeCommonCaptureFiles(capture);
    writeTextFile(capture.path() / "payload.jsonl", "");
    writeTextFile(capture.path() / "frames.jsonl", "");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_EQ(result.valueIf()->seed.microtimestamp, 1000U);
    EXPECT_EQ(result.valueIf()->checkpoint.microtimestamp, 2000U);
    EXPECT_TRUE(result.valueIf()->jc_captureOrderEvents.empty());
    EXPECT_TRUE(result.valueIf()->jc_tradeEvents.empty());
}

TEST(BitstampJoinedCapture, RejectsMalformedFrameJson) {
    const TempCaptureDirectory capture{"te_joined_capture_malformed_frame_json"};

    writeCommonCaptureFiles(capture);
    writeTextFile(capture.path() / "payload.jsonl", "{}\n");
    writeTextFile(capture.path() / "frames.jsonl", "\n");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::bitstamp::JoinedCaptureError::frame_malformed);
}

TEST(BitstampJoinedCapture, AcceptsValidFrameJson) {
    const TempCaptureDirectory capture{"te_joined_capture_valid_frame_json"};

    writeCommonCaptureFiles(capture);
    writeTextFile(capture.path() / "payload.jsonl", "{}\n");
    writeTextFile(capture.path() / "frames.jsonl", R"({"streamKind":"control"})"
                                                   "\n");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_TRUE(result.valueIf()->jc_captureOrderEvents.empty());
    EXPECT_TRUE(result.valueIf()->jc_tradeEvents.empty());
}

TEST(BitstampJoinedCapture, AppendsDecodedEventsToJcVectors) {
    const TempCaptureDirectory capture{"te_joined_capture_appends_decoded_events"};

    writeCommonCaptureFiles(capture);
    writeTextFile(capture.path() / "payload.jsonl",
                  std::string{kOrderPayload} + "\n" + std::string{kTradePayload} + "\n");
    writeTextFile(capture.path() / "frames.jsonl", R"({"streamKind":"order"})"
                                                   "\n"
                                                   R"({"streamKind":"trade"})"
                                                   "\n");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    ASSERT_EQ(result.valueIf()->jc_captureOrderEvents.size(), 1U);
    ASSERT_EQ(result.valueIf()->jc_tradeEvents.size(), 1U);
    EXPECT_EQ(result.valueIf()->jc_captureOrderEvents.front().event.order_id,
              te::OrderId{2037493297635328ULL});
    EXPECT_EQ(result.valueIf()->jc_captureOrderEvents.front().amountTraded, te::Qty{});
    EXPECT_EQ(result.valueIf()->jc_tradeEvents.front().buy_order_id,
              te::OrderId{2041200416022642ULL});
}

TEST(BitstampJoinedCapture, LoadedCaptureReplaysToCheckpoint) {
    const TempCaptureDirectory capture{"te_joined_capture_replays_to_checkpoint"};

    constexpr std::string_view seed =
        R"({"timestamp":"1","microtimestamp":"1000","bids":[["100.00","2.00000000","42"]],"asks":[]})";
    constexpr std::string_view checkpoint =
        R"({"timestamp":"2","microtimestamp":"2500","bids":[["100.00","1.50000000","42"]],"asks":[["101.00","1.00000000","77"]]})";
    constexpr std::string_view orderPayload =
        R"({"data":{"id_str":"77","order_type":1,"microtimestamp":"1500","price_str":"101.00","amount_str":"1.00000000","amount_traded":"0.00000000"},"event":"order_created"})";
    constexpr std::string_view tradePayload =
        R"({"data":{"microtimestamp":"2000","buy_order_id":42,"sell_order_id":999,"amount_str":"0.50000000"},"event":"trade"})";

    writeTextFile(capture.path() / "manifest.json", kManifest);
    writeTextFile(capture.path() / "seed.snapshot", seed);
    writeTextFile(capture.path() / "checkpoint.snapshot", checkpoint);
    writeTextFile(capture.path() / "payload.jsonl",
                  std::string{orderPayload} + "\n" + std::string{tradePayload} + "\n");
    writeTextFile(capture.path() / "frames.jsonl", R"({"streamKind":"order"})"
                                                   "\n"
                                                   R"({"streamKind":"trade"})"
                                                   "\n");

    const auto loaded = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());
    ASSERT_TRUE(loaded.hasValue());
    ASSERT_NE(loaded.valueIf(), nullptr);
    const te::bitstamp::JoinedCapture& joinedCapture = *loaded.valueIf();

    te::bitstamp::Replay replay;
    const auto replayed =
        replay.replay(joinedCapture.seed, joinedCapture.jc_captureOrderEvents,
                      joinedCapture.jc_tradeEvents, joinedCapture.checkpoint.microtimestamp);

    ASSERT_TRUE(replayed.hasValue());
    ASSERT_NE(replayed.valueIf(), nullptr);
    EXPECT_EQ(replayed.valueIf()->stats.orderEventsApplied, 1U);
    EXPECT_EQ(replayed.valueIf()->stats.tradeEventsRead, 1U);
    EXPECT_EQ(replayed.valueIf()->stats.correctionsApplied, 1U);

    replayed.valueIf()->book.validate();
    expectBookMatchesSnapshot(replayed.valueIf()->book, joinedCapture.checkpoint);
}

}  // namespace
