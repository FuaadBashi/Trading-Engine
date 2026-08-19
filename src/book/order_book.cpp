#include <cassert>
#include <numeric>
#include <te/book/order_book.hpp>

namespace te {

Result<ApplyOutcome, ApplyError> OrderBook::apply(const OrderEvent& orderEvent){

    ApplyOutcome applyOutcome;
    if( orderIndex_.contains(orderEvent.order_id) ){
        if (orderEvent.kind == EventKind::add) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::duplicate_order_id);
        } 
    } else if ( !orderIndex_.contains(orderEvent.order_id) ) {
        if (orderEvent.kind == EventKind::modify || orderEvent.kind == EventKind::remove) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::unknown_order_id);
        }
    }

    

    // Case 1 add order
    if (orderEvent.kind == EventKind::add)
    {
        
        auto& levels =(orderEvent.side == Side::buy) ? bids_ : asks_;
        if (orderEvent.quantity.units <= 0){
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::invalid_quantity);
        }

        if (orderEvent.price.ticks <= 0){
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::invalid_price);
        }

        auto [levelIt, createdLevel] = levels.try_emplace(orderEvent.price);
        std::optional<OrderHandle> orderHandle  = levelIt -> second.addOrder(orderEvent.order_id, orderEvent.quantity);
        if (!orderHandle.has_value()){
            if(createdLevel){
                levels.erase(levelIt);
            }
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::level_quantity_overflow); 
            
        }
        OrderLocator orderLocator = {
                orderEvent.side,
                orderEvent.price,
                *orderHandle

            };
            applyOutcome.createdLevel = createdLevel;
            orderIndex_.emplace(orderEvent.order_id, orderLocator);
            return Result<ApplyOutcome, ApplyError>::success( applyOutcome);
    }

    // Case 2 change order
    if (orderEvent.kind == EventKind::modify)
    {
       
        if (orderEvent.quantity.units <= 0){
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::invalid_quantity);
        }
        if (orderEvent.price.ticks <= 0){
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::invalid_price);
        }
        auto orderIt = orderIndex_.find(orderEvent.order_id);
        assert(orderIt != orderIndex_.end());
        OrderLocator& locator = orderIt->second;
        if(orderEvent.side != locator.side){
            return  Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::side_mismatch);
        }
        OrderHandle orderHandle = locator.order_pos;
        auto& levels =(locator.side == Side::buy) ? bids_ : asks_;
        auto oldLevelIt = levels.find(locator.price);
        if (oldLevelIt == levels.end()){
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::unknown_order_id);
        }

        if(orderEvent.price == locator.price){
            bool quantityChanged  = oldLevelIt->second.changeQty(orderHandle,orderEvent.quantity);
            if(!quantityChanged) {
                return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::level_quantity_overflow);
            }
            return Result<ApplyOutcome, ApplyError>::success( applyOutcome);
        } else {
            auto [targetLevelIt, createdLevel] = levels.try_emplace(orderEvent.price);
            std::optional<OrderHandle> newOrderHandle  = targetLevelIt -> second.addOrder(orderEvent.order_id, orderEvent.quantity);
            if (!newOrderHandle.has_value()){
                if(createdLevel){
                    levels.erase(targetLevelIt);
                }
                return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::level_quantity_overflow); 
            }

            oldLevelIt->second.removeOrder(locator.order_pos);
            if (oldLevelIt->second.isEmpty()){
                levels.erase(oldLevelIt);
                applyOutcome.removedLevel = true;
            }

            locator.price = orderEvent.price;
            locator.order_pos = *newOrderHandle;
            applyOutcome.createdLevel = createdLevel;

            return Result<ApplyOutcome, ApplyError>::success( applyOutcome);
        }
    }
    
    // Case 3 remove order
    if (orderEvent.kind == EventKind::remove)
    {
        auto orderIt = orderIndex_.find(orderEvent.order_id);
        assert(orderIt != orderIndex_.end());
        OrderLocator& locator = orderIt->second;
        if (orderEvent.side != locator.side) {
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::side_mismatch);
        }

        auto& levels =(locator.side == Side::buy) ? bids_ : asks_;
        auto levelIt = levels.find(locator.price);
        if (levelIt == levels.end()){
            return Result<ApplyOutcome, ApplyError>::failure(te::ApplyError::unknown_order_id);
        }

        levelIt->second.removeOrder(locator.order_pos);

        if (levelIt->second.isEmpty()){
            levels.erase(levelIt);
            applyOutcome.removedLevel = true;
        }

        orderIndex_.erase(orderIt);
        return Result<ApplyOutcome, ApplyError>::success( applyOutcome);  
    }



    return Result<ApplyOutcome, ApplyError>::failure( ApplyError::side_mismatch);


}
void OrderBook::validate() const {
    //1) BestBid !<= BestAsk:
    assert(!bestBid().has_value() || !bestAsk().has_value() || *bestBid() < *bestAsk());
    
    //2) PriceLevel exists at all Prices:[x]
    //3) Locator.pos -> index == PriceLevel.OrderID [x]
    //4) Actualy Total_Qty == Expect Total_Qty:

   for (const auto& [id, locator] : orderIndex_) {
        [[maybe_unused]] auto& levels = (locator.side == Side::buy) ? bids_ : asks_;

        assert(levels.find(locator.price)!= levels.end());
        assert(locator.order_pos->id == id);
    }
    
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



}
