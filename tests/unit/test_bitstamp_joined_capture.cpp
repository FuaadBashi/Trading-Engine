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
    writeTextFile(capture.path() / "frames.jsonl", R"({"captureOrdinal":1,"streamKind":"control"})"
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
    writeTextFile(capture.path() / "frames.jsonl", R"({"captureOrdinal":1,"streamKind":"order"})"
                                                   "\n"
                                                   R"({"captureOrdinal":2,"streamKind":"trade"})"
                                                   "\n");

    const auto result = te::bitstamp::loadJoinedCapture(capture.path(), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    ASSERT_EQ(result.valueIf()->jc_captureOrderEvents.size(), 1U);
    ASSERT_EQ(result.valueIf()->jc_tradeEvents.size(), 1U);
    EXPECT_EQ(result.valueIf()->jc_captureOrderEvents.front().event.order_id,
              te::OrderId{2037493297635328ULL});
    EXPECT_EQ(result.valueIf()->jc_captureOrderEvents.front().amountTraded, te::Qty{});
    EXPECT_EQ(result.valueIf()->jc_tradeEvents.front().event.buy_order_id,
              te::OrderId{2041200416022642ULL});
}

TEST(BitstampJoinedCapture, LoadedCaptureReplaysToCheckpoint) {
    const TempCaptureDirectory capture{"te_joined_capture_replays_to_checkpoint"};

    constexpr std::string_view seed =
        R"({"timestamp":"1","microtimestamp":"1000","bids":[["100.00","2.00000000","42"]],"asks":[]})";
    constexpr std::string_view checkpoint =
        R"({"timestamp":"2","microtimestamp":"2500","bids":[["100.00","1.50000000","42"]],"asks":[["101.00","1.00000000","77"]]})";
    constexpr std::string_view orderPayload =
        R"({"data":{"id":77,"id_str":"77","order_type":1,"microtimestamp":"1500","price_str":"101.00","amount_str":"1.00000000","amount_traded":"0.00000000"},"event":"order_created"})";
    constexpr std::string_view tradePayload =
        R"({"data":{"microtimestamp":"2000","buy_order_id":42,"sell_order_id":999,"amount_str":"0.50000000"},"event":"trade"})";

    writeTextFile(capture.path() / "manifest.json", kManifest);
    writeTextFile(capture.path() / "seed.snapshot", seed);
    writeTextFile(capture.path() / "checkpoint.snapshot", checkpoint);
    writeTextFile(capture.path() / "payload.jsonl",
                  std::string{orderPayload} + "\n" + std::string{tradePayload} + "\n");
    writeTextFile(capture.path() / "frames.jsonl", R"({"captureOrdinal":1,"streamKind":"order"})"
                                                   "\n"
                                                   R"({"captureOrdinal":2,"streamKind":"trade"})"
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

// MANDATORY. Unlike every other real-data test in this file, this one never skips: the fixture is
// committed, so a clean checkout on any machine exercises loader -> bootstrap -> merge ->
// reconciler -> book end to end. Before it existed a fresh checkout reported every test green while
// silently skipping the entire real pipeline.
//
// The checkpoint in the fixture is hand-written from what the venue would report, never produced by
// replaying this code. A fixture whose expected state came from the book under test would only
// prove the book agrees with itself.
//
// It also covers a path no real Bitstamp capture has ever reached: order 102 is consumed by a trade
// that live_orders never reports, so TradeReconciler must manufacture the removal. Across 1,059
// seconds of real joined capture that case occurred zero times (ADR 0013).
TEST(BitstampJoinedCapture, GoldenFixtureReplaysToHandWrittenCheckpoint) {
    const std::filesystem::path capture =
        std::filesystem::path(TE_TEST_DATA_DIR) / "joined-capture-golden";
    ASSERT_TRUE(std::filesystem::exists(capture / "manifest.json"))
        << "committed fixture missing -- it is mandatory, not optional: " << capture;

    const auto loaded = te::bitstamp::loadJoinedCapture(capture, btcUsd());
    ASSERT_TRUE(loaded.hasValue());
    const te::bitstamp::JoinedCapture& joinedCapture = *loaded.valueIf();
    EXPECT_EQ(joinedCapture.jc_captureOrderEvents.size(), 5U);
    EXPECT_EQ(joinedCapture.jc_tradeEvents.size(), 2U);

    // captureOrdinal is carried through the loader even though it is not the tie-break.
    EXPECT_EQ(joinedCapture.jc_captureOrderEvents.front().captureOrdinal, 1U);

    te::bitstamp::Replay replay;
    const auto replayed =
        replay.replay(joinedCapture.seed, joinedCapture.jc_captureOrderEvents,
                      joinedCapture.jc_tradeEvents, joinedCapture.checkpoint.microtimestamp);
    ASSERT_TRUE(replayed.hasValue())
        << "replay failed with error " << (replayed.errorIf() ? int(*replayed.errorIf()) : -1);
    const te::bitstamp::ReplayResult& result = *replayed.valueIf();

    // One event before the seed, one after the checkpoint, three in the window.
    EXPECT_EQ(result.stats.orderEventsBeforeSeed, 1U);
    EXPECT_EQ(result.stats.orderEventsRead, 3U);
    EXPECT_EQ(result.stats.orderEventsAfterCutoff, 1U);
    EXPECT_EQ(result.stats.orderEventsAccountedFor(),
              joinedCapture.jc_captureOrderEvents.size());
    EXPECT_EQ(result.stats.tradeEventsAccountedFor(), joinedCapture.jc_tradeEvents.size());

    // The 1200000 fill is reported by both streams, so its credit absorbs the trade. The 1300000
    // fill is reported only by live_trades, so exactly one correction is manufactured.
    EXPECT_EQ(result.stats.tradeEventsRead, 2U);
    EXPECT_EQ(result.stats.correctionsGenerated, 1U);
    EXPECT_EQ(result.stats.correctionsApplied, 1U);
    EXPECT_EQ(result.stats.reconciler.ordersRemovedWithUnmatchedFill, 0U);
    EXPECT_EQ(result.stats.reconciler.staleFillsDiscarded, 0U);

    result.book.validate();
    expectBookMatchesSnapshot(result.book, joinedCapture.checkpoint);
}

// The real-corpus gate: a full joined capture replayed from its own S0 seed must reproduce the
// independently fetched S1 snapshot exactly, with no hand-listed exceptions. The synthetic test
// above proves the wiring; only this one proves the merge, classifier and fill accounting against
// 29k real events. Capture data is gitignored, so this skips rather than fails when absent.
TEST(BitstampJoinedCapture, RealCaptureReplaysToCheckpointWithNoResiduals) {
    const std::filesystem::path capture =
        std::filesystem::path(TE_PROJECT_ROOT_DIR) / "data/raw/bitstamp-btcusd-20260822T000512Z";
    if (!std::filesystem::exists(capture / "manifest.json")) {
        GTEST_SKIP() << "OPTIONAL EVIDENCE NOT RUN: venue agreement over a 29k-event joined\n                        capture is unverified on this machine. The committed golden fixture\n                        still proves the pipeline. Missing: " << capture;
    }

    const auto loaded = te::bitstamp::loadJoinedCapture(capture, btcUsd());
    ASSERT_TRUE(loaded.hasValue());
    const te::bitstamp::JoinedCapture& joinedCapture = *loaded.valueIf();
    EXPECT_EQ(joinedCapture.jc_captureOrderEvents.size(), 29404U);
    EXPECT_EQ(joinedCapture.jc_tradeEvents.size(), 84U);

    te::bitstamp::Replay replay;
    const auto replayed =
        replay.replay(joinedCapture.seed, joinedCapture.jc_captureOrderEvents,
                      joinedCapture.jc_tradeEvents, joinedCapture.checkpoint.microtimestamp);

    // Before ADR 0013 this returned unexpected_order_apply_failure: nine live orders were deleted
    // by corrections that double-counted a fill live_orders had already reported.
    ASSERT_TRUE(replayed.hasValue())
        << "replay failed with error " << (replayed.errorIf() ? int(*replayed.errorIf()) : -1);
    const te::bitstamp::ReplayResult& result = *replayed.valueIf();

    // Every in-window order event survives classification on this capture.
    EXPECT_EQ(result.stats.orderEventsRead, 25196U);
    EXPECT_EQ(result.stats.orderEventsApplied, 25196U);
    EXPECT_EQ(result.stats.tradeEventsRead, 82U);

    // live_orders reports every fill here, so the credit covers all 82 trades and the reconciler
    // has nothing to correct. This capture therefore does NOT exercise the correction path; the
    // unit tests in test_trade_reconciler.cpp are what cover it.
    EXPECT_EQ(result.stats.correctionsGenerated, 0U);
    EXPECT_EQ(result.stats.correctionsApplied, 0U);
    EXPECT_EQ(result.stats.redundantOrderRemovals, 0U);

    // Both health counters clean: every reported fill was matched by a trade.
    EXPECT_EQ(result.stats.reconciler.ordersRemovedWithUnmatchedFill, 0U);
    EXPECT_EQ(result.stats.reconciler.staleFillsDiscarded, 0U);

    // Plan v4 s12: every input accounted for exactly once. Without this a merge-loop bug that
    // silently dropped events would leave no trace anywhere.
    EXPECT_EQ(result.stats.orderEventsAccountedFor(),
              joinedCapture.jc_captureOrderEvents.size());
    EXPECT_EQ(result.stats.tradeEventsAccountedFor(), joinedCapture.jc_tradeEvents.size());
    EXPECT_EQ(result.stats.orderEventsBeforeSeed, 1U);
    EXPECT_EQ(result.stats.orderEventsAfterCutoff, 4207U);

    result.book.validate();
    expectBookMatchesSnapshot(result.book, joinedCapture.checkpoint);
}

}  // namespace
