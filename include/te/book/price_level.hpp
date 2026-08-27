#pragma once

#include <list>
#include <optional>

#include "te/core/types.hpp"

namespace te {

struct RestingOrder {
    OrderId id{};
    Qty qty{};
};

using OrderHandle = std::list<RestingOrder>::iterator;

// Owns FIFO order nodes at one price and caches their exact aggregate quantity.
// List iterators stay valid until their own order is removed, enabling OrderLocator.
class PriceLevel {
public:
    // nullopt/false means invalid quantity or aggregate overflow; state is unchanged.
    std::optional<OrderHandle> addOrder(OrderId id, Qty quantity);
    void removeOrder(OrderHandle orderHandle);
    bool changeQty(OrderHandle orderHandle, Qty newQty);

    Qty totalQuantity() const { return total_quantity_; }
    bool isEmpty() const;
    auto begin() const { return restingOrders_.begin(); }
    auto end() const { return restingOrders_.end(); }

private:
    std::list<RestingOrder> restingOrders_{};
    Qty total_quantity_{};
};

}  // namespace te
