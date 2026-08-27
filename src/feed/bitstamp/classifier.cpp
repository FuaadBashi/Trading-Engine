#include <te/feed/bitstamp/classifier.hpp>

namespace te::bitstamp {

EventDisposition EventClassifier::classify(const OrderEvent& event) {
    // A price-zero create opens a lifecycle that never belongs in the book. Remember its ID
    // because later messages in the same lifecycle otherwise look like ordinary orders.
    if (event.kind == EventKind::add) {
        if (event.price == Price{0}) {
            zeroPriceOrders_.insert(event.order_id);
            ++stats_.zeroPriceLifecycle;
            return EventDisposition::zero_price_lifecycle;
        }
        ++stats_.appliedToBook;
        return EventDisposition::apply_to_book;
    }

    // Filter the entire remembered lifecycle, even when later messages carry a plausible price.
    const auto tracked = zeroPriceOrders_.find(event.order_id);
    if (tracked != zeroPriceOrders_.end()) {
        ++stats_.zeroPriceLifecycle;
        if (event.kind == EventKind::remove) {
            // The terminal event releases the remembered ID.
            zeroPriceOrders_.erase(tracked);
        }
        return EventDisposition::zero_price_lifecycle;
    }

    ++stats_.appliedToBook;
    return EventDisposition::apply_to_book;
}

}  // namespace te::bitstamp
