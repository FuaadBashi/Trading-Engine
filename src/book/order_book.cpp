#include <cassert>
#include <te/book/order_book.hpp>

namespace te {

Result<ApplyOutcome, ApplyError> OrderBook::apply(const OrderEvent& orderEvent) {
    if (orderEvent.side != Side::buy && orderEvent.side != Side::sell) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::invalid_side);
    }

    // Reject ID-state contradictions before selecting a side or touching a price level.
    if (orderIndex_.contains(orderEvent.order_id)) {
        if (orderEvent.kind == EventKind::add) {
            return Result<ApplyOutcome, ApplyError>::failure(ApplyError::duplicate_order_id);
        }
    } else {
        if (orderEvent.kind == EventKind::modify || orderEvent.kind == EventKind::remove) {
            return Result<ApplyOutcome, ApplyError>::failure(ApplyError::unknown_order_id);
        }
    }

    const auto result = [&]() -> Result<ApplyOutcome, ApplyError> {
        switch (orderEvent.kind) {
            case EventKind::add:
                return applyAdd(orderEvent);

            case EventKind::modify:
                return applyModify(orderEvent);

            case EventKind::remove:
                return applyRemove(orderEvent);
        }

        return Result<ApplyOutcome, ApplyError>::failure(
            ApplyError::invalid_event_kind);
    }();

#ifndef NDEBUG
    // A full walk is O(book): check every event while the book is small, every 256th once large.
    constexpr std::uint64_t kFullCheckInterval = 256;
    ++debugAppliedEvents_;
    if (result.hasValue() && (orderIndex_.size() <= kFullCheckInterval ||
                              debugAppliedEvents_ % kFullCheckInterval == 0)) {
        validateStructure();
    }
#endif

    return result;
}

Result<ApplyOutcome, ApplyError> OrderBook::applyAdd(const OrderEvent& orderEvent) {
    if (orderEvent.quantity.units <= 0) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::invalid_quantity);
    }
    if (orderEvent.price.ticks <= 0) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::invalid_price);
    }

    // Allocation failure is fatal and unhandled here; catching it leaves a ghost level (ADR 0015).
    auto& levels = (orderEvent.side == Side::buy) ? bids_ : asks_;
    auto [levelIt, createdLevel] = levels.try_emplace(orderEvent.price);
    const std::optional<OrderHandle> orderHandle =
        levelIt->second.addOrder(orderEvent.order_id, orderEvent.quantity);
    if (!orderHandle.has_value()) {
        // Roll back a level created only for this failed insertion.
        if (createdLevel) {
            levels.erase(levelIt);
        }
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::level_quantity_overflow);
    }

    const OrderLocator orderLocator{
        .side = orderEvent.side,
        .price = orderEvent.price,
        .order_pos = *orderHandle,
    };
    orderIndex_.emplace(orderEvent.order_id, orderLocator);

    return Result<ApplyOutcome, ApplyError>::success(
        ApplyOutcome{.createdLevel = createdLevel});
}

Result<ApplyOutcome, ApplyError> OrderBook::applyModify(const OrderEvent& orderEvent) {
    if (orderEvent.quantity.units <= 0) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::invalid_quantity);
    }
    if (orderEvent.price.ticks <= 0) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::invalid_price);
    }

    auto orderIt = orderIndex_.find(orderEvent.order_id);
    // Unreachable after apply()'s guard, but GCC -O2 cannot prove it (-Wnull-dereference).
    if (orderIt == orderIndex_.end()) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::unknown_order_id);
    }
    OrderLocator& locator = orderIt->second;
    if (orderEvent.side != locator.side) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::side_mismatch);
    }

    auto& levels = (locator.side == Side::buy) ? bids_ : asks_;
    auto oldLevelIt = levels.find(locator.price);
    if (oldLevelIt == levels.end()) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::unknown_order_id);
    }

    if (orderEvent.price == locator.price) {
        // A quantity-only change keeps the existing list node and therefore its position.
        const bool quantityChanged =
            oldLevelIt->second.changeQty(locator.order_pos, orderEvent.quantity);
        if (!quantityChanged) {
            return Result<ApplyOutcome, ApplyError>::failure(
                ApplyError::level_quantity_overflow);
        }
        return Result<ApplyOutcome, ApplyError>::success(ApplyOutcome{});
    }

    // Insert at the destination first, so an overflow leaves the original order untouched.
    // A throw between here and removeOrder() would leave the order in two levels (ADR 0015).
    auto [targetLevelIt, createdLevel] = levels.try_emplace(orderEvent.price);
    const std::optional<OrderHandle> newOrderHandle =
        targetLevelIt->second.addOrder(orderEvent.order_id, orderEvent.quantity);
    if (!newOrderHandle.has_value()) {
        if (createdLevel) {
            levels.erase(targetLevelIt);
        }
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::level_quantity_overflow);
    }

    ApplyOutcome outcome{.createdLevel = createdLevel};
    oldLevelIt->second.removeOrder(locator.order_pos);
    if (oldLevelIt->second.isEmpty()) {
        levels.erase(oldLevelIt);
        outcome.removedLevel = true;
    }

    locator.price = orderEvent.price;
    locator.order_pos = *newOrderHandle;
    return Result<ApplyOutcome, ApplyError>::success(outcome);
}

Result<ApplyOutcome, ApplyError> OrderBook::applyRemove(const OrderEvent& orderEvent) {
    auto orderIt = orderIndex_.find(orderEvent.order_id);
    // Unreachable after apply()'s guard; kept for GCC -Wnull-dereference, as in applyModify.
    if (orderIt == orderIndex_.end()) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::unknown_order_id);
    }
    const OrderLocator& locator = orderIt->second;
    if (orderEvent.side != locator.side) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::side_mismatch);
    }

    // The stored locator is authoritative; a delete message's price and quantity are not.
    auto& levels = (locator.side == Side::buy) ? bids_ : asks_;
    auto levelIt = levels.find(locator.price);
    if (levelIt == levels.end()) {
        return Result<ApplyOutcome, ApplyError>::failure(ApplyError::unknown_order_id);
    }

    levelIt->second.removeOrder(locator.order_pos);

    ApplyOutcome outcome{};
    if (levelIt->second.isEmpty()) {
        levels.erase(levelIt);
        outcome.removedLevel = true;
    }

    orderIndex_.erase(orderIt);
    return Result<ApplyOutcome, ApplyError>::success(outcome);
}
std::optional<Price> OrderBook::bestBid() const noexcept {
    if (bids_.empty()) {
        return std::nullopt;
    }
    return bids_.rbegin()->first;
}

std::optional<Price> OrderBook::bestAsk() const noexcept {
    if (asks_.empty()) {
        return std::nullopt;
    }
    return asks_.begin()->first;
}

Qty OrderBook::qtyAt(Side side, Price price) const noexcept {
    const auto& levels = (side == Side::buy) ? bids_ : asks_;
    const auto levelIt = levels.find(price);
    if (levelIt == levels.end()) {
        return Qty{};
    }
    return levelIt->second.totalQuantity();
}

void OrderBook::validateStructure() const {
    // Every index entry names an existing level and points to the order with its key's ID.
    for (const auto& [orderId, locator] : orderIndex_) {
        [[maybe_unused]] const auto& levels = (locator.side == Side::buy) ? bids_ : asks_;
        assert(levels.find(locator.price) != levels.end());
        assert(locator.order_pos->id == orderId);
    }

    // Every order points back to this exact side, price, and list node, and totals are exact.
    const auto validateLevels = [this]([[maybe_unused]] Side expectedSide,
                                       const auto& levels) {
        for (const auto& [price, level] : levels) {
            assert(!level.isEmpty());

            Qty currentQty{0};
            for (auto orderIt = level.begin(); orderIt != level.end(); ++orderIt) {
                const auto indexIt = orderIndex_.find(orderIt->id);
                assert(indexIt != orderIndex_.end());

                [[maybe_unused]] const OrderLocator& locator = indexIt->second;
                assert(locator.side == expectedSide);
                assert(locator.price == price);
                assert(locator.order_pos == orderIt);

                currentQty.units += orderIt->qty.units;
            }
            assert(level.totalQuantity() == currentQty);
        }
    };

    validateLevels(Side::buy, bids_);
    validateLevels(Side::sell, asks_);
}

MarketShape OrderBook::marketShape() const noexcept {
    const std::optional<Price> bid = bestBid();
    const std::optional<Price> ask = bestAsk();

    if (!bid.has_value() && !ask.has_value()) {
        return MarketShape::empty;
    }
    if (!bid.has_value() || !ask.has_value()) {
        return MarketShape::one_sided;
    }
    if (*bid == *ask) {
        return MarketShape::locked;
    }
    if (*bid > *ask) {
        return MarketShape::crossed;
    }
    return MarketShape::open;
}
std::uint64_t OrderBook::digest() const noexcept {
    // Not standard FNV-1a: the basis is missing a digit, and recorded digests depend on it.
    // Bump kBookDigestVersion before changing it. std::map order makes the walk canonical.
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
