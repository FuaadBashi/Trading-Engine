#pragma once

#include <string_view>

#include <te/core/instrument.hpp>
#include <te/core/result.hpp>
#include <te/feed/bitstamp_decoder.hpp>  // reuses DecoderError
#include <te/feed/trade_event.hpp>

namespace te {

/** @brief Decodes one Bitstamp live_trades WebSocket payload into a TradeEvent. */
Result<TradeEvent, DecoderError> decodeBitstampTrade(std::string_view text, InstrumentSpec spec);

}  // namespace te
