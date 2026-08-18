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

class PriceLevel {
public:
    std::optional<OrderHandle> addOrder(OrderId id, Qty quantity);
    void removeOrder(OrderHandle orderHandle);
    bool changeQty(OrderHandle orderHandle, Qty newQty);

    Qty totalQuantity() const { return total_quantity_; }
    bool isEmpty() const;

private:
    std::list<RestingOrder> restingOrders_;
    Qty total_quantity_{};
};

}  // namespace te
