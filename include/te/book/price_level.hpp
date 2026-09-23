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
    [[nodiscard]] std::optional<OrderHandle> addOrder(OrderId id, Qty quantity);
    void removeOrder(OrderHandle orderHandle);
    [[nodiscard]] bool changeQty(OrderHandle orderHandle, Qty newQty);

    [[nodiscard]] Qty totalQuantity() const noexcept { return total_quantity_; }
    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] auto begin() const noexcept { return restingOrders_.begin(); }
    [[nodiscard]] auto end() const noexcept { return restingOrders_.end(); }

private:
    std::list<RestingOrder> restingOrders_{};
    Qty total_quantity_{};
};

}  // namespace te
