#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "te/capture/segment_loader.hpp"

namespace {

class TempSegmentDirectory {
public:
    explicit TempSegmentDirectory(std::string_view name)
        : path_{std::filesystem::temp_directory_path() / name} {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempSegmentDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempSegmentDirectory(const TempSegmentDirectory&) = delete;
    TempSegmentDirectory& operator=(const TempSegmentDirectory&) = delete;

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
    ASSERT_TRUE(output.is_open());
    output << contents;
    ASSERT_TRUE(output.good());
}

te::SegmentDescription makeDescription(const std::filesystem::path& directory,
                                       bool withCheckpoint) {
    return te::SegmentDescription{
        .index = 0,
        .payloadPath = directory / "payload.jsonl",
        .frameIndexPath = directory / "frames.jsonl",
        .seedPath = directory / "seed.snapshot",
        .checkpointPath = withCheckpoint
                              ? std::optional<std::filesystem::path>{directory /
                                                                     "checkpoint.snapshot"}
                              : std::nullopt,
    };
}

void writeRequiredFiles(const TempSegmentDirectory& capture) {
    writeTextFile(capture.path() / "seed.snapshot",
                  R"({"timestamp":"1","microtimestamp":"1000","bids":[],"asks":[]})");
    writeTextFile(capture.path() / "payload.jsonl", "");
    writeTextFile(capture.path() / "frames.jsonl", "");
}

TEST(SegmentLoader, LoadsSegmentWithCheckpoint) {
    const TempSegmentDirectory capture{"te_segment_loader_with_checkpoint"};
    writeRequiredFiles(capture);
    writeTextFile(capture.path() / "checkpoint.snapshot",
                  R"({"timestamp":"2","microtimestamp":"2000","bids":[],"asks":[]})");

    const auto result = te::loadSegment(makeDescription(capture.path(), true), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_EQ(result.valueIf()->seed.microtimestamp, 1000U);
    ASSERT_TRUE(result.valueIf()->checkpoint.has_value());
    EXPECT_EQ(result.valueIf()->checkpoint->microtimestamp, 2000U);
}

TEST(SegmentLoader, LoadsSegmentWithoutCheckpoint) {
    const TempSegmentDirectory capture{"te_segment_loader_without_checkpoint"};
    writeRequiredFiles(capture);

    const auto result = te::loadSegment(makeDescription(capture.path(), false), btcUsd());

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    EXPECT_EQ(result.valueIf()->seed.microtimestamp, 1000U);
    EXPECT_FALSE(result.valueIf()->checkpoint.has_value());
}

}  // namespace
