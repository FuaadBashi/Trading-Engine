#include "te/feed/bitstamp/bootstrap.hpp"
#include <te/feed/trade_reconciler.hpp>

namespace te::bitstamp {

    Result<OrderBook, ApplyError> bootstrap(BookSnapshot book, TradeReconciler* reconciler){
        OrderBook orderBook;

        for(size_t i {}; i < book.orders.size(); ++i){
            SnapshotOrder snapshotOrder = book.orders.at(i);

            const OrderEvent &orderEvent {
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
           

            if(reconciler){
                reconciler->observe(orderEvent);
            }
        }
        orderBook.validate();

        return  Result<OrderBook, ApplyError>::success(std::move(orderBook));

    };

}  // namespace te::bitstamp