#pragma once

#include <cstddef>
#include <istream>
#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/core/time.hpp>
#include <te/telemetry/sink.hpp>

namespace te {

// Per-run accounting makes dropped or silently skipped input visible. Gap records are extra
// output records, so the checked invariant subtracts gapsDetected from written.
struct RecorderStats {
    std::size_t linesRead{};
    std::size_t written{};
    std::size_t skipped{};
    std::size_t failed{};
    std::size_t gapsDetected{};
};

enum class RecorderError {
    // Fatal: the output is truncated and the stream remains failed.
    sink_write_failed,
    counter_mismatch,
};

// Testable stream-to-stream capture loop. It flushes each record and checks Bitstamp's order-event
// chain; a mismatch writes an in-stream gap marker before the event that exposed it (ADR 0006).
Result<RecorderStats, RecorderError> runRecorder(std::istream& input, Sink& sink,
                                                 const InstrumentSpec& spec, const Clock& clock);

}  // namespace te
