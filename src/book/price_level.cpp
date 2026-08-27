#include <cassert>
#include <limits>
#include <te/book/price_level.hpp>

namespace te {

std::optional<OrderHandle> PriceLevel::addOrder(OrderId id, Qty quantity) {
    if (quantity.units <= 0) {
        return std::nullopt;
    }
    if (total_quantity_.units > std::numeric_limits<std::int64_t>::max() - quantity.units) {
        return std::nullopt;
    }

    te::RestingOrder restingOrder{
        id,
        quantity,
    };
    total_quantity_.units += quantity.units;
    return restingOrders_.insert(restingOrders_.end(), restingOrder);
}

void PriceLevel::removeOrder(OrderHandle orderHandle) {
    assert(total_quantity_.units >= orderHandle->qty.units);
    total_quantity_.units -= orderHandle->qty.units;
    restingOrders_.erase(orderHandle);
}

bool PriceLevel::changeQty(OrderHandle orderHandle, Qty newQty) {
    assert(total_quantity_.units >= orderHandle->qty.units);
    if (newQty.units <= 0) {
        return false;
    }

    const std::int64_t withoutOld = total_quantity_.units - orderHandle->qty.units;
    // Compute the candidate total before mutating either the aggregate or the order node.
    if (withoutOld > std::numeric_limits<std::int64_t>::max() - newQty.units) {
        return false;
    }

    total_quantity_.units = withoutOld + newQty.units;
    orderHandle->qty = newQty;
    return true;
}

bool PriceLevel::isEmpty() const { return restingOrders_.empty(); }

}  // namespace te
