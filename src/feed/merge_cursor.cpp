#include "te/feed/merge_cursor.hpp"

namespace te {

MergeCursor::MergeCursor(std::span<const CapturedOrderEvent> orderEvents,
                         std::span<const CapturedTradeEvent> tradeEvents,
                         std::uint64_t seedMicros, std::uint64_t cutoffMicros)
    : orderEvents_{orderEvents}, tradeEvents_{tradeEvents}, cutoffMicros_{cutoffMicros} {
    while (orderPtr_ < orderEvents_.size() &&
           orderEvents_[orderPtr_].event.venue_timestamp_us <= seedMicros) {
        ++orderPtr_;
    }
    ordersBeforeSeed_ = orderPtr_;

    while (tradePtr_ < tradeEvents_.size() &&
           tradeEvents_[tradePtr_].event.venue_timestamp_us <= seedMicros) {
        ++tradePtr_;
    }
    tradesBeforeSeed_ = tradePtr_;
}

bool MergeCursor::orderInWindow() const noexcept {
    return orderPtr_ < orderEvents_.size() &&
           orderEvents_[orderPtr_].event.venue_timestamp_us <= cutoffMicros_;
}

bool MergeCursor::tradeInWindow() const noexcept {
    return tradePtr_ < tradeEvents_.size() &&
           tradeEvents_[tradePtr_].event.venue_timestamp_us <= cutoffMicros_;
}

std::optional<MergedPick> MergeCursor::next() {
    const bool orderAvailable = orderInWindow();
    const bool tradeAvailable = tradeInWindow();
    if (!orderAvailable && !tradeAvailable) {
        return std::nullopt;
    }

    // The whole tie-break lives in this one comparison: `<=` hands a shared timestamp to the order
    // event. Nothing else in the project is allowed to decide this.
    if (orderAvailable && (!tradeAvailable ||
                           orderEvents_[orderPtr_].event.venue_timestamp_us <=
                               tradeEvents_[tradePtr_].event.venue_timestamp_us)) {
        return MergedPick{MergedStream::order, orderPtr_++};
    }
    return MergedPick{MergedStream::trade, tradePtr_++};
}

std::size_t MergeCursor::ordersAfterCutoff() const noexcept {
    return orderEvents_.size() - orderPtr_;
}

std::size_t MergeCursor::tradesAfterCutoff() const noexcept {
    return tradeEvents_.size() - tradePtr_;
}

}  // namespace te
