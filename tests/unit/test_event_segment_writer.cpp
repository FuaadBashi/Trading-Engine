#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <type_traits>

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
