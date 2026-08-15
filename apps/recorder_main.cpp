#include <cstdio>     
#include <fstream>      
#include <string>       
#include <string_view>

#include <te/core/instrument.hpp>
#include <te/core/time.hpp>
#include <te/feed/bitstamp_decoder.hpp>
#include <te/telemetry/record.hpp>
#include <te/telemetry/sink.hpp>

int main(int argc, char** argv) {

    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <input.jsonl> <output.bin>\n", argv[0]);
        return 1;
    }
    const std::string inputPath  = argv[1];
    const std::string outputPath = argv[2];

    te::InstrumentSpec spec{
        .venue_id = te::VenueId::bitstamp,
        .instrument_id = te::InstrumentId::btc_usd,
        .price_decimals = 2,
        .quantity_decimals = 8,
    };
    
    std::ifstream input(inputPath);
    if (!input.is_open()) {
        // report the failure
        std::fprintf(stderr, "cannot open input: %s\n", inputPath.c_str());
        return 1;
    }

    auto opened = te::Sink::open(outputPath);
    if (!opened.hasValue()) {
        std::fprintf(stderr, "cannot open output: %s\n", outputPath.c_str());
        return 1;
    }
    te::Sink* sink = opened.valueIf();  

    te::Clock clock = te::makeSystemClock();

    std::size_t linesRead = 0;
    std::size_t written   = 0;
    std::size_t skipped   = 0;
    std::size_t failed    = 0;

    std::string line;
    while (std::getline(input, line)) {
        ++linesRead;
        auto result = te::decodeBitstampEvent(line, spec);
        if (result.hasValue()) {
            te::Record record = te::buildRecord(*result.valueIf(), clock);

            if (!sink->write(record)) {
                std::fprintf(stderr, "write failed after %zu records (input line %zu)\n",
                            written, linesRead);
                return 1;
            }
            if (!sink->flush()) {
                std::fprintf(stderr, "flush failed after %zu records (input line %zu)\n",
                            written, linesRead);
                return 1;
            }
            ++written;
        } else {

            switch (*result.errorIf()) {
            case te::DecoderError::not_order_event:
                ++skipped;
                break;
            case te::DecoderError::malformed_json:
            case te::DecoderError::missing_field:
            case te::DecoderError::invalid_field:
                ++failed;
                break;

             }
        }
    }

    if (linesRead != written + skipped + failed){
        std::fprintf(stderr, "counter mismatch: %zu lines vs %zu accounted\n",
                    linesRead, written + skipped + failed);
        return 1;
    } 

    std::printf("lines read: %zu\nwritten: %zu\nskipped: %zu\nfailed: %zu\n",
            linesRead, written, skipped, failed);

    return 0;
}
