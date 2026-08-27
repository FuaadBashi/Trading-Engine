#pragma once

#include <string_view>
#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/feed/bitstamp/decoder.hpp>  // reuses DecoderError
#include <te/feed/trade_event.hpp>

namespace te::bitstamp {

// Reads amount_str exactly; buy/sell order IDs are JSON integers in this channel.
Result<TradeEvent, DecoderError> decodeTrade(std::string_view text, InstrumentSpec spec);

}  // namespace te::bitstamp
