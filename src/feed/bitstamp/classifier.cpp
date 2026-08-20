#include <te/feed/bitstamp/classifier.hpp>

namespace te::bitstamp {

EventDisposition EventClassifier::classify(const OrderEvent& event) {
    // A price-zero create opens a lifecycle that never belongs in the book. Only the id is
    // kept, because the events that follow it look ordinary and cannot be recognised any other
    // way. This mirrors scripts/audit_book_bootstrap.py, the Python oracle the C++ side is
    // checked against; the two must agree on which events reach the book, or the comparison
    // proves nothing.
    if (event.kind == EventKind::add) {
        if (event.price == Price{0}) {
            zeroPriceOrders_.insert(event.order_id);
            ++stats_.zeroPriceLifecycle;
            return EventDisposition::zero_price_lifecycle;
        }
        ++stats_.appliedToBook;
        return EventDisposition::apply_to_book;
    }

    // Modify/remove for an id already known to be a price-zero lifecycle. These arrive with a
    // plausible-looking price, so without the remembered id they would be applied to the book
    // as real orders.
    const auto tracked = zeroPriceOrders_.find(event.order_id);
    if (tracked != zeroPriceOrders_.end()) {
        ++stats_.zeroPriceLifecycle;
        if (event.kind == EventKind::remove) {
            // Lifecycle over; stop tracking so the set does not grow without bound.
            zeroPriceOrders_.erase(tracked);
        }
        return EventDisposition::zero_price_lifecycle;
    }

    ++stats_.appliedToBook;
    return EventDisposition::apply_to_book;
}

}  // namespace te::bitstamp
