// Slice 1. Reads a venue JSON capture, decodes it, writes a binary capture.
// A thin shell: argument handling, path safety and reporting only. The capture loop itself
// lives in te_core (telemetry/recorder.hpp) so it can be tested without a filesystem.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include <te/core/instrument.hpp>
#include <te/core/time.hpp>
#include <te/telemetry/recorder.hpp>
#include <te/telemetry/sink.hpp>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <input.jsonl> <output.bin>\n", argv[0]);
        return 1;
    }
    const std::string inputPath = argv[1];
    const std::string outputPath = argv[2];

    // Sink appends rather than truncating, so writing to an existing capture would silently
    // concatenate two runs into one file that looks like a single continuous session. Refuse
    // instead: the caller can delete or rename deliberately, but must not do it by accident.
    if (std::filesystem::exists(outputPath)) {
        std::fprintf(stderr, "output already exists, refusing to append: %s\n",
                     outputPath.c_str());
        return 1;
    }

    std::ifstream input(inputPath);
    if (!input.is_open()) {
        std::fprintf(stderr, "cannot open input: %s\n", inputPath.c_str());
        return 1;
    }

    auto opened = te::Sink::open(outputPath);
    if (!opened.hasValue()) {
        std::fprintf(stderr, "cannot open output: %s\n", outputPath.c_str());
        return 1;
    }

    const te::InstrumentSpec spec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };

    const te::Clock clock = te::makeSystemClock();

    const auto result = te::runRecorder(input, *opened.valueIf(), spec, clock);

    if (!result.hasValue()) {
        switch (*result.errorIf()) {
            case te::RecorderError::sink_write_failed:
                std::fprintf(stderr, "write to capture failed; output is truncated: %s\n",
                             outputPath.c_str());
                break;
            case te::RecorderError::counter_mismatch:
                std::fprintf(stderr, "internal error: input lines did not match outcomes\n");
                break;
        }
        return 1;
    }

    const te::RecorderStats& stats = *result.valueIf();
    std::printf("lines read: %zu\nwritten: %zu\nskipped: %zu\nfailed: %zu\n",
                stats.linesRead, stats.written, stats.skipped, stats.failed);

    return 0;
}
