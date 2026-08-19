#include "te/feed/bitstamp_bootstrap.hpp"

namespace te {


    Result<OrderBook, ApplyError> bootstrapBitstampEvent(BookSnapshot book){
        OrderBook orderBook;
        for(size_t i {}; i < book.orders.size(); ++i){
            SnapshotOrder snapshotOrder = book.orders.at(i);

            const OrderEvent &orderEvent = { 
                book.microtimestamp,
                snapshotOrder.order_id,
                snapshotOrder.price,
                snapshotOrder.quantity,
                snapshotOrder.side,
                EventKind::add,
            };

            const auto result = orderBook.apply(orderEvent);
            if(!result.hasValue()){
                return  Result<OrderBook, ApplyError>::failure( *result.errorIf());
            }
        }
        orderBook.validate();

        return  Result<OrderBook, ApplyError>::success(std::move(orderBook));

    };


}