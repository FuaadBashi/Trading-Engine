#pragma once

#include <cstddef>
#include <istream>

#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/core/time.hpp>
#include <te/telemetry/sink.hpp>

// Slice 1. The capture loop: venue JSON lines in, binary Records out.

namespace te {

/**
 * @brief  Outcome counts for one capture run.
 *
 * @note   These are the run's only observability surface. A capture that silently dropped half
 *         its input produces a file that looks entirely healthy; only the counts reveal it.
 *
 * @note   linesRead must always equal written + skipped + failed. The two sides are counted
 *         independently -- one per input line, one per branch taken -- so a disagreement means a
 *         line took a path that accounted for nothing.
 */
struct RecorderStats {
    /** @brief Lines pulled from the input stream, including blank and non-order lines. */
    std::size_t linesRead{};

    /** @brief Records successfully written to the sink. */
    std::size_t written{};

    /**
     * @brief  Lines that were valid but not order events.
     *
     * @note   Not failures. Bitstamp's live_orders channel carries protocol messages such as
     *         bts:subscription_succeeded; every healthy capture contains at least one. Counting
     *         them as errors would make every clean run look broken, which trains you to ignore
     *         the error count.
     */
    std::size_t skipped{};

    /** @brief Lines that should have decoded into an order event but could not. */
    std::size_t failed{};
};

/**
 * @brief  Reason a capture run could not complete.
 */
enum class RecorderError {
    /**
     * @brief  A write or flush to the sink failed.
     *
     * @note   Fatal, unlike a bad input line. The output file is now truncated mid-capture, and
     *         the sink's stream stays in a failed state, so continuing would silently discard
     *         every remaining record.
     */
    sink_write_failed,

    /**
     * @brief  linesRead did not equal written + skipped + failed.
     *
     * @note   Should be unreachable; it exists so that if it ever happens it is loud rather than
     *         a quietly short capture.
     */
    counter_mismatch,
};

/**
 * @brief  Decodes every line of @p input and appends the resulting Records to @p sink.
 *
 * @param  input Venue JSON, one message per line.
 * @param  sink  Destination for the encoded records.
 * @param  spec  Decimal scales used to interpret price and quantity text exactly.
 * @param  clock Supplies each record's receipt timestamp.
 *
 * @return Counts for the run, or the reason it aborted.
 *
 * @note   Takes streams rather than paths so the loop is testable without touching the
 *         filesystem: a test passes a std::istringstream of synthetic lines. Path handling,
 *         argument parsing and reporting belong to the calling program, not here.
 *
 * @note   Flushes after every record. That is the caller-visible durability choice: the window
 *         of loss on an unclean shutdown is zero records, at the cost of a syscall per event.
 *
 * @note   Blank lines are skipped without being decoded and count toward neither written nor
 *         failed; they are counted as skipped, since an empty line is not a malformed event.
 */
Result<RecorderStats, RecorderError> runRecorder(std::istream& input, Sink& sink,
                                                 const InstrumentSpec& spec, const Clock& clock);

}  // namespace te
