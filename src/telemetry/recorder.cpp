#include <te/telemetry/recorder.hpp>

#include <string>

#include <te/feed/bitstamp/decoder.hpp>
#include <te/telemetry/record.hpp>

namespace te {

Result<RecorderStats, RecorderError> runRecorder(std::istream& input, Sink& sink,
                                                 const InstrumentSpec& spec, const Clock& clock) {
    RecorderStats stats{};
    std::string line;

    // Continuity state. The first order event has no predecessor, so hasPreviousLink stays false
    // until one has been seen; without that guard every capture would open with a false gap.
    bitstamp::ChainLink previousLink{};
    bool hasPreviousLink = false;

    while (std::getline(input, line)) {
        ++stats.linesRead;

        if (line.empty()) {
            ++stats.skipped;
            continue;
        }

        const auto decoded = bitstamp::decodeOrder(line, spec);

        if (decoded.hasValue()) {
            // Chain ids exist only on order lifecycle messages, so this is checked here rather
            // than per line. A missing or unreadable chain is not treated as a gap: absence of
            // evidence is not evidence of loss, and inventing gaps would censor good sessions.
            const auto chain = bitstamp::decodeChain(line);
            if (chain.hasValue()) {
                if (hasPreviousLink && chain.valueIf()->pre_event_id != previousLink.event_id) {
                    const Record gap = buildGapRecord(clock);
                    if (!sink.write(gap) || !sink.flush()) {
                        return Result<RecorderStats, RecorderError>::failure(
                            RecorderError::sink_write_failed);
                    }
                    ++stats.gapsDetected;
                    ++stats.written;
                }
                previousLink = *chain.valueIf();
                hasPreviousLink = true;
            }

            const Record record = buildRecord(*decoded.valueIf(), clock);

            if (!sink.write(record) || !sink.flush()) {
                return Result<RecorderStats, RecorderError>::failure(
                    RecorderError::sink_write_failed);
            }
            ++stats.written;
        } else {
            switch (*decoded.errorIf()) {
                case bitstamp::DecoderError::not_order_event:
                    ++stats.skipped;
                    break;
                case bitstamp::DecoderError::malformed_json:
                case bitstamp::DecoderError::missing_field:
                case bitstamp::DecoderError::invalid_field:
                    ++stats.failed;
                    break;
            }
        }
    }

    // Gap markers are written records without a corresponding input line, so they are subtracted
    // before the per-line accounting is checked.
    if (stats.linesRead != (stats.written - stats.gapsDetected) + stats.skipped + stats.failed) {
        return Result<RecorderStats, RecorderError>::failure(RecorderError::counter_mismatch);
    }

    return Result<RecorderStats, RecorderError>::success(stats);
}

}  // namespace te
