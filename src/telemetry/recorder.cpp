#include <te/telemetry/recorder.hpp>

#include <string>

#include <te/feed/bitstamp_decoder.hpp>
#include <te/telemetry/record.hpp>

namespace te {

Result<RecorderStats, RecorderError> runRecorder(std::istream& input, Sink& sink,
                                                 const InstrumentSpec& spec, const Clock& clock) {
    RecorderStats stats{};
    std::string line;

    while (std::getline(input, line)) {
        ++stats.linesRead;

        if (line.empty()) {
            ++stats.skipped;
            continue;
        }

        const auto decoded = decodeBitstampEvent(line, spec);

        if (decoded.hasValue()) {
            const Record record = buildRecord(*decoded.valueIf(), clock);

            if (!sink.write(record) || !sink.flush()) {
                return Result<RecorderStats, RecorderError>::failure(
                    RecorderError::sink_write_failed);
            }
            ++stats.written;
        } else {
            switch (*decoded.errorIf()) {
                case DecoderError::not_order_event:
                    ++stats.skipped;
                    break;
                case DecoderError::malformed_json:
                case DecoderError::missing_field:
                case DecoderError::invalid_field:
                    ++stats.failed;
                    break;
            }
        }
    }

    if (stats.linesRead != stats.written + stats.skipped + stats.failed) {
        return Result<RecorderStats, RecorderError>::failure(RecorderError::counter_mismatch);
    }

    return Result<RecorderStats, RecorderError>::success(stats);
}

}  // namespace te
