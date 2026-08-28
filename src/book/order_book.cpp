#include <cassert>
#include <te/book/order_book.hpp>

namespace te {

Result<ApplyOutcome, ApplyError> OrderBook::apply(const OrderEvent& orderEvent) {
    ApplyOutcome applyOutcome;
    // Reject ID-state contradictions before selecting a side or touching a price level.
    if (orderIndex_.contains(orderEvent.order_id)) {
        if (orderEvent.kind == EventKind::add) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::duplicate_order_id);
        }
    } else if (!orderIndex_.contains(orderEvent.order_id)) {
        if (orderEvent.kind == EventKind::modify || orderEvent.kind == EventKind::remove) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::unknown_order_id);
        }
    }

    if (orderEvent.kind == EventKind::add) {
        auto& levels = (orderEvent.side == Side::buy) ? bids_ : asks_;
        if (orderEvent.quantity.units <= 0) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::invalid_quantity);
        }

        if (orderEvent.price.ticks <= 0) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::invalid_price);
        }

        auto [levelIt, createdLevel] = levels.try_emplace(orderEvent.price);
        std::optional<OrderHandle> orderHandle =
            levelIt->second.addOrder(orderEvent.order_id, orderEvent.quantity);
        if (!orderHandle.has_value()) {
            // Roll back a level created solely for this failed insertion.
            if (createdLevel) {
                levels.erase(levelIt);
            }
            return Result<ApplyOutcome, ApplyError>::failure(
                te::ApplyError::level_quantity_overflow);
        }
        OrderLocator orderLocator = {orderEvent.side, orderEvent.price, *orderHandle

        };
        applyOutcome.createdLevel = createdLevel;
        orderIndex_.emplace(orderEvent.order_id, orderLocator);
        return Result<ApplyOutcome, ApplyError>::success(applyOutcome);
    }

    if (orderEvent.kind == EventKind::modify) {
        if (orderEvent.quantity.units <= 0) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::invalid_quantity);
        }
        if (orderEvent.price.ticks <= 0) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::invalid_price);
        }
        auto orderIt = orderIndex_.find(orderEvent.order_id);
        assert(orderIt != orderIndex_.end());
        OrderLocator& locator = orderIt->second;
        if (orderEvent.side != locator.side) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::side_mismatch);
        }
        OrderHandle orderHandle = locator.order_pos;
        auto& levels = (locator.side == Side::buy) ? bids_ : asks_;
        auto oldLevelIt = levels.find(locator.price);
        if (oldLevelIt == levels.end()) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::unknown_order_id);
        }

        if (orderEvent.price == locator.price) {
            // A quantity-only change keeps the existing list node and therefore its position.
            bool quantityChanged = oldLevelIt->second.changeQty(orderHandle, orderEvent.quantity);
            if (!quantityChanged) {
                return Result<ApplyOutcome, ApplyError>::failure(
                    te::ApplyError::level_quantity_overflow);
            }
            return Result<ApplyOutcome, ApplyError>::success(applyOutcome);
        } else {
            // Insert at the destination first. If it overflows, the original order is untouched.
            auto [targetLevelIt, createdLevel] = levels.try_emplace(orderEvent.price);
            std::optional<OrderHandle> newOrderHandle =
                targetLevelIt->second.addOrder(orderEvent.order_id, orderEvent.quantity);
            if (!newOrderHandle.has_value()) {
                if (createdLevel) {
                    levels.erase(targetLevelIt);
                }
                return Result<ApplyOutcome, ApplyError>::failure(
                    te::ApplyError::level_quantity_overflow);
            }

            oldLevelIt->second.removeOrder(locator.order_pos);
            if (oldLevelIt->second.isEmpty()) {
                levels.erase(oldLevelIt);
                applyOutcome.removedLevel = true;
            }

            locator.price = orderEvent.price;
            locator.order_pos = *newOrderHandle;
            applyOutcome.createdLevel = createdLevel;

            return Result<ApplyOutcome, ApplyError>::success(applyOutcome);
        }
    }

    if (orderEvent.kind == EventKind::remove) {
        auto orderIt = orderIndex_.find(orderEvent.order_id);
        assert(orderIt != orderIndex_.end());
        OrderLocator& locator = orderIt->second;
        if (orderEvent.side != locator.side) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::side_mismatch);
        }

        // The stored locator is authoritative; a delete message's price and quantity are not.
        auto& levels = (locator.side == Side::buy) ? bids_ : asks_;
        auto levelIt = levels.find(locator.price);
        if (levelIt == levels.end()) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::unknown_order_id);
        }

        levelIt->second.removeOrder(locator.order_pos);

        if (levelIt->second.isEmpty()) {
            levels.erase(levelIt);
            applyOutcome.removedLevel = true;
        }

        orderIndex_.erase(orderIt);
        return Result<ApplyOutcome, ApplyError>::success(applyOutcome);
    }

    return Result<ApplyOutcome, ApplyError>::failure(ApplyError::side_mismatch);
}
void OrderBook::validate() const {
    // A valid resting book is not crossed.
    assert(!bestBid().has_value() || !bestAsk().has_value() || *bestBid() < *bestAsk());

    // Every index entry must point into the level and order it claims to locate.
    for (const auto& [id, locator] : orderIndex_) {
        [[maybe_unused]] auto& levels = (locator.side == Side::buy) ? bids_ : asks_;

        assert(levels.find(locator.price) != levels.end());
        assert(locator.order_pos->id == id);
    }

    // Every level is non-empty, indexed, and has an exact aggregate quantity.
    for (const auto& [price, level] : bids_) {
        assert(level.isEmpty() == false);

        Qty current_qty = {0};
        for (const auto& order : level) {
            assert(orderIndex_.contains(order.id));
            current_qty.units += order.qty.units;
        }
        assert(level.totalQuantity() == current_qty);
    }
    for (const auto& [price, level] : asks_) {
        assert(level.isEmpty() == false);
        Qty current_qty = {0};
        for (const auto& order : level) {
            assert(orderIndex_.contains(order.id));
            current_qty.units += order.qty.units;
        }
        assert(level.totalQuantity() == current_qty);
    }
}

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.rbegin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

Qty OrderBook::qtyAt(Side side, Price price) const {
    const auto& levels = (side == Side::buy) ? bids_ : asks_;
    const auto levelIt = levels.find(price);
    if (levelIt == levels.end()) {
        return Qty{};
    }
    return levelIt->second.totalQuantity();
}

std::uint64_t OrderBook::digest() const {
    // FNV-1a. std::map iterates in ascending price order, so the walk is already canonical and
    // does not depend on the order events arrived in.
    constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    const auto mix = [](std::uint64_t digest, std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            digest ^= (value >> (byte * 8)) & 0xFFULL;
            digest *= kPrime;
        }
        return digest;
    };

    std::uint64_t digest = kOffsetBasis;
    for (const auto& [side, levels] :
         {std::pair{Side::buy, &bids_}, std::pair{Side::sell, &asks_}}) {
        for (const auto& [price, level] : *levels) {
            // An empty level is indistinguishable from an absent one for qtyAt, so it must not
            // change the digest either.
            if (level.totalQuantity().units == 0) {
                continue;
            }
            digest = mix(digest, static_cast<std::uint64_t>(side));
            digest = mix(digest, static_cast<std::uint64_t>(price.ticks));
            digest = mix(digest, static_cast<std::uint64_t>(level.totalQuantity().units));
        }
    }
    return digest;
}

}  // namespace te
