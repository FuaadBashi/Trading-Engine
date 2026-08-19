#pragma once

#include <list>
#include <optional>

#include "te/book/order_book.hpp"
#include "te/feed/bitstamp_snapshot.hpp"

namespace te {

    Result<OrderBook, ApplyError> bootstrapBitstampEvent(BookSnapshot parseText);


}