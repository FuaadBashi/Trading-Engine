#pragma once

#include <cstdint>
#include <filesystem>

#include "te/capture/segment_loader.hpp"
#include "te/core/result.hpp"
#include "te/telemetry/segment_format.hpp"

namespace te {

enum class TapeWriteError {
    header_not_event_format,
    output_open_failed,
    record_append_failed,
    finish_failed,
};

// Every input event lands in exactly one of beforeSeed / written / afterCutoff, so the three sum to
// the input size. Same accounting discipline as ReplayStats, for the same reason: a merge bug that
// silently dropped an event would otherwise leave no trace.
struct TapeWriteStats {
    std::uint64_t ordersWritten{};
    std::uint64_t tradesWritten{};
    std::uint64_t ordersBeforeSeed{};
    std::uint64_t tradesBeforeSeed{};
    std::uint64_t ordersAfterCutoff{};
    std::uint64_t tradesAfterCutoff{};
    std::uint64_t bytesWritten{};

    std::uint64_t ordersAccountedFor() const {
        return ordersBeforeSeed + ordersWritten + ordersAfterCutoff;
    }
    std::uint64_t tradesAccountedFor() const {
        return tradesBeforeSeed + tradesWritten + tradesAfterCutoff;
    }
};

// Writes one L1 tape: the capture's order and trade events merged into deterministic replay order
// and encoded as portable v3 records.
//
// "L1" means the tape carries raw merged events. Only the *merge* is precomputed — classification,
// book application and trade reconciliation still run on read. Baking those in as well was
// considered and deliberately deferred: the reconciler's correction path has never fired on real
// data (ADR 0013), and freezing an unexercised policy into stored bytes is the mistake that ADR's
// own history warns about.
//
// Events in (seedMicros, cutoffMicros] are written. The caller supplies venue, instrument, scales,
// seed timestamp and snapshot hash on `header`; this function stamps the ordering policy itself,
// because this function *is* the policy and the caller cannot know it.
//
// KNOWN GAP: a tape does not carry the pre-seed events that train the stateful classifier, so
// replaying a tape is not yet proven to reproduce replaying the raw capture. See
// `docs/specs/v3-segment-format.md`, "Classifier warm-up".
Result<TapeWriteStats, TapeWriteError> writeEventTape(const JoinedCapture& capture,
                                                      const SegmentHeader& header,
                                                      std::uint64_t seedMicros,
                                                      std::uint64_t cutoffMicros,
                                                      const std::filesystem::path& outputPath);

}  // namespace te
