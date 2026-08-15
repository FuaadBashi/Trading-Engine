#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <te/telemetry/record.hpp>
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

/// The event used throughout the decoder and text_to_int tests, from a real captured line.
te::OrderEvent sampleEvent() {
    return te::OrderEvent{
        .venue_timestamp_us = 1786269861947000ULL,
        .order_id = te::OrderId{2037493297635328ULL},
        .price = te::Price{5835610},
        .quantity = te::Qty{171371},
        .side = te::Side::buy,
        .kind = te::EventKind::remove,
    };
}

te::Clock fixedClock(std::int64_t nanos) {
    te::Clock clock;
    clock.now = [nanos]() { return te::Nanos{nanos}; };
    return clock;
}

/// Reads every whole Record out of a file, in write order.
std::vector<te::Record> readAllRecords(const std::string& path) {
    std::vector<te::Record> records;
    std::ifstream in(path, std::ios::binary);

    te::Record record{};
    while (in.read(reinterpret_cast<char*>(&record), sizeof(record))) {
        records.push_back(record);
    }
    return records;
}

void expectSameRecord(const te::Record& actual, const te::Record& expected) {
    EXPECT_EQ(actual.version, expected.version);
    EXPECT_EQ(actual.receipt_timestamp_us, expected.receipt_timestamp_us);
    EXPECT_EQ(actual.orderEvent.venue_timestamp_us, expected.orderEvent.venue_timestamp_us);
    EXPECT_EQ(actual.orderEvent.order_id, expected.orderEvent.order_id);
    EXPECT_EQ(actual.orderEvent.price, expected.orderEvent.price);
    EXPECT_EQ(actual.orderEvent.quantity, expected.orderEvent.quantity);
    EXPECT_EQ(actual.orderEvent.side, expected.orderEvent.side);
    EXPECT_EQ(actual.orderEvent.kind, expected.orderEvent.kind);
}

}  // namespace

// The point of the whole capture path: a Record written to disk must read back byte-identical.
TEST(RecordRoundtrip, SingleRecordSurvivesWriteAndRead) {
    const TempFile file("te_roundtrip_single.bin");
    const te::Record written = te::buildRecord(sampleEvent(), fixedClock(42));

    {
        auto opened = te::Sink::open(file.path());
        ASSERT_TRUE(opened.hasValue());
        te::Sink* sink = opened.valueIf();
        ASSERT_NE(sink, nullptr);

        EXPECT_TRUE(sink->write(written));
        EXPECT_TRUE(sink->flush());
    }

    const std::vector<te::Record> readBack = readAllRecords(file.path());
    ASSERT_EQ(readBack.size(), 1U);
    expectSameRecord(readBack[0], written);
}

// Byte-level equality, not just field-by-field: catches padding bytes differing, which
// field comparisons would silently miss.
TEST(RecordRoundtrip, BytesOnDiskMatchTheRecordExactly) {
    const TempFile file("te_roundtrip_bytes.bin");
    const te::Record written = te::buildRecord(sampleEvent(), fixedClock(7));

    {
        auto opened = te::Sink::open(file.path());
        ASSERT_TRUE(opened.hasValue());
        ASSERT_TRUE(opened.valueIf()->write(written));
    }

    const std::vector<te::Record> readBack = readAllRecords(file.path());
    ASSERT_EQ(readBack.size(), 1U);
    EXPECT_EQ(std::memcmp(&readBack[0], &written, sizeof(te::Record)), 0);
}

// Append-only means order is preserved and nothing overwrites anything earlier.
TEST(RecordRoundtrip, MultipleRecordsKeepWriteOrder) {
    const TempFile file("te_roundtrip_order.bin");

    te::OrderEvent second = sampleEvent();
    second.order_id = te::OrderId{999ULL};
    second.side = te::Side::sell;
    second.kind = te::EventKind::add;

    const te::Record first = te::buildRecord(sampleEvent(), fixedClock(1));
    const te::Record secondRecord = te::buildRecord(second, fixedClock(2));

    {
        auto opened = te::Sink::open(file.path());
        ASSERT_TRUE(opened.hasValue());
        te::Sink* sink = opened.valueIf();
        ASSERT_TRUE(sink->write(first));
        ASSERT_TRUE(sink->write(secondRecord));
        ASSERT_TRUE(sink->flush());
    }

    const std::vector<te::Record> readBack = readAllRecords(file.path());
    ASSERT_EQ(readBack.size(), 2U);
    expectSameRecord(readBack[0], first);
    expectSameRecord(readBack[1], secondRecord);
}

// A fixed-size format means "record N" is findable by arithmetic; that only holds if the file
// is exactly sizeof(Record) per record, with no delimiters or padding between them.
TEST(RecordRoundtrip, FileSizeIsExactlyRecordSizeTimesCount) {
    const TempFile file("te_roundtrip_size.bin");
    const te::Record record = te::buildRecord(sampleEvent(), fixedClock(3));

    {
        auto opened = te::Sink::open(file.path());
        ASSERT_TRUE(opened.hasValue());
        te::Sink* sink = opened.valueIf();
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(sink->write(record));
        }
        ASSERT_TRUE(sink->flush());
    }

    EXPECT_EQ(std::filesystem::file_size(file.path()), sizeof(te::Record) * 3);
}

// Reopening must append, not truncate: a capture continued across two runs keeps run one.
TEST(RecordRoundtrip, ReopeningAppendsRatherThanTruncating) {
    const TempFile file("te_roundtrip_reopen.bin");
    const te::Record record = te::buildRecord(sampleEvent(), fixedClock(5));

    {
        auto first = te::Sink::open(file.path());
        ASSERT_TRUE(first.hasValue());
        ASSERT_TRUE(first.valueIf()->write(record));
        ASSERT_TRUE(first.valueIf()->flush());
    }
    {
        auto second = te::Sink::open(file.path());
        ASSERT_TRUE(second.hasValue());
        ASSERT_TRUE(second.valueIf()->write(record));
        ASSERT_TRUE(second.valueIf()->flush());
    }

    EXPECT_EQ(readAllRecords(file.path()).size(), 2U);
}

TEST(Sink, ReportsOkOnAFreshlyOpenedSink) {
    const TempFile file("te_sink_ok.bin");

    auto opened = te::Sink::open(file.path());
    ASSERT_TRUE(opened.hasValue());
    EXPECT_TRUE(opened.valueIf()->ok());
}

TEST(Sink, RejectsAPathInAMissingDirectory) {
    const std::string path =
        (std::filesystem::temp_directory_path() / "te_no_such_dir_xyz" / "capture.bin").string();

    auto opened = te::Sink::open(path);

    ASSERT_FALSE(opened.hasValue());
    ASSERT_NE(opened.errorIf(), nullptr);
    EXPECT_EQ(*opened.errorIf(), te::SinkError::cannot_open);
}

TEST(Sink, RejectsAPathThatNamesADirectory) {
    const std::string path = std::filesystem::temp_directory_path().string();

    auto opened = te::Sink::open(path);

    ASSERT_FALSE(opened.hasValue());
    ASSERT_NE(opened.errorIf(), nullptr);
    EXPECT_EQ(*opened.errorIf(), te::SinkError::cannot_open);
}
