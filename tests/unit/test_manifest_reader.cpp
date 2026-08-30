#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

#include "te/feed/manifest_reader.hpp"

namespace {

class TempManifestDirectory {
public:
    explicit TempManifestDirectory(std::string_view name)
        : path_{std::filesystem::temp_directory_path() / name} {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempManifestDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TempManifestDirectory(const TempManifestDirectory&) = delete;
    TempManifestDirectory& operator=(const TempManifestDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void writeManifest(const TempManifestDirectory& capture, std::string_view contents) {
    std::ofstream output{capture.path() / "manifest.json", std::ios::binary};
    ASSERT_TRUE(output.is_open());
    output << contents;
    ASSERT_TRUE(output.good());
}

TEST(ManifestReader, ReadsOneSegmentAndBuildsCompletePaths) {
    const TempManifestDirectory capture{"te_manifest_reader_one_segment"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot",
        "checkpoint": "checkpoint-0000.snapshot"
      }]
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_TRUE(result.hasValue());
    ASSERT_NE(result.valueIf(), nullptr);
    const te::CaptureManifest& manifest = *result.valueIf();
    EXPECT_EQ(manifest.formatVersion, 2U);
    EXPECT_EQ(manifest.venue, te::VenueId::bitstamp);
    EXPECT_EQ(manifest.instrument, te::InstrumentId::btc_usd);
    ASSERT_EQ(manifest.segments.size(), 1U);

    const te::SegmentDescription& segment = manifest.segments.front();
    EXPECT_EQ(segment.index, 0U);
    EXPECT_EQ(segment.payloadPath, capture.path() / "segment-0000.jsonl");
    EXPECT_EQ(segment.frameIndexPath, capture.path() / "segment-0000.frames.jsonl");
    EXPECT_EQ(segment.seedPath, capture.path() / "segment-0000.snapshot");
    ASSERT_TRUE(segment.checkpointPath.has_value());
    EXPECT_EQ(*segment.checkpointPath, capture.path() / "checkpoint-0000.snapshot");
}

TEST(ManifestReader, PreservesMultipleSegmentsInChronologicalOrder) {
    const TempManifestDirectory capture{"te_manifest_reader_multiple_segments"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [
        {
          "index": 0,
          "payload": "segment-0000.jsonl",
          "frame_index": "segment-0000.frames.jsonl",
          "snapshot": "segment-0000.snapshot",
          "checkpoint": "checkpoint-0000.snapshot"
        },
        {
          "index": 1,
          "payload": "segment-0001.jsonl",
          "frame_index": "segment-0001.frames.jsonl",
          "snapshot": "segment-0001.snapshot",
          "checkpoint": "checkpoint-0001.snapshot"
        }
      ]
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.valueIf()->segments.size(), 2U);
    EXPECT_EQ(result.valueIf()->segments.at(0).index, 0U);
    EXPECT_EQ(result.valueIf()->segments.at(1).index, 1U);
    EXPECT_EQ(result.valueIf()->segments.at(1).payloadPath,
              capture.path() / "segment-0001.jsonl");
}

TEST(ManifestReader, AllowsSegmentWithoutCheckpoint) {
    const TempManifestDirectory capture{"te_manifest_reader_without_checkpoint"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment-0000.jsonl",
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot"
      }]
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_TRUE(result.hasValue());
    ASSERT_EQ(result.valueIf()->segments.size(), 1U);
    EXPECT_FALSE(result.valueIf()->segments.front().checkpointPath.has_value());
}

TEST(ManifestReader, RejectsEmptySegmentsArray) {
    const TempManifestDirectory capture{"te_manifest_reader_empty_segments"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": []
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ManifestError::manifest_missing_segment);
}

TEST(ManifestReader, RejectsMissingRequiredSegmentField) {
    const TempManifestDirectory capture{"te_manifest_reader_missing_field"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "frame_index": "segment-0000.frames.jsonl",
        "snapshot": "segment-0000.snapshot"
      }]
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ManifestError::manifest_missing_field);
}

TEST(ManifestReader, RejectsUnsupportedFormatVersion) {
    const TempManifestDirectory capture{"te_manifest_reader_unsupported_version"};
    writeManifest(capture, R"({
      "format_version": 3,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": []
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ManifestError::unsupported_format_version);
}

TEST(ManifestReader, RejectsNonContiguousSegmentIndexes) {
    const TempManifestDirectory capture{"te_manifest_reader_index_gap"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [
        {
          "index": 0,
          "payload": "segment-0000.jsonl",
          "frame_index": "segment-0000.frames.jsonl",
          "snapshot": "segment-0000.snapshot"
        },
        {
          "index": 2,
          "payload": "segment-0002.jsonl",
          "frame_index": "segment-0002.frames.jsonl",
          "snapshot": "segment-0002.snapshot"
        }
      ]
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ManifestError::mismatch_index);
}

TEST(ManifestReader, RejectsNegativeSegmentIndex) {
    const TempManifestDirectory capture{"te_manifest_reader_negative_index"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "btcusd",
      "segments": [{
        "index": -1,
        "payload": "segment.jsonl",
        "frame_index": "segment.frames.jsonl",
        "snapshot": "segment.snapshot"
      }]
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ManifestError::manifest_missing_field);
}

TEST(ManifestReader, RejectsUnknownVenue) {
    const TempManifestDirectory capture{"te_manifest_reader_unknown_venue"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "unknown_exchange",
      "instrument": "btcusd",
      "segments": [{
        "index": 0,
        "payload": "segment.jsonl",
        "frame_index": "segment.frames.jsonl",
        "snapshot": "segment.snapshot"
      }]
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ManifestError::manifest_invalid_structure);
}

TEST(ManifestReader, RejectsUnknownInstrument) {
    const TempManifestDirectory capture{"te_manifest_reader_unknown_instrument"};
    writeManifest(capture, R"({
      "format_version": 2,
      "venue": "bitstamp",
      "instrument": "unknown_pair",
      "segments": [{
        "index": 0,
        "payload": "segment.jsonl",
        "frame_index": "segment.frames.jsonl",
        "snapshot": "segment.snapshot"
      }]
    })");

    const auto result = te::manifestReader(capture.path());

    ASSERT_FALSE(result.hasValue());
    ASSERT_NE(result.errorIf(), nullptr);
    EXPECT_EQ(*result.errorIf(), te::ManifestError::manifest_invalid_structure);
}

}  // namespace
