#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "te/feed/captured_events.hpp"

namespace te {

enum class MergedStream : std::uint8_t {
    order,
    trade,
};

struct MergedPick {
    MergedStream stream;
    std::size_t index;
};

// Deterministic two-pointer merge of two individually time-ordered capture streams.
//
// The cursor owns the ordering decision and nothing else: no book, no classifier, no reconciler.
// That is the point. Live replay and the v3 tape writer both need ADR 0013's tie-break, and a
// policy written twice is a policy that eventually disagrees with itself.
//
// Exact venue-timestamp ties resolve in favour of the order event, so a trade at time T sees an
// order that became resting at T. ADR 0013 explains why the opposite tie-break breaks a different
// case rather than fixing this one.
//
// Borrows both streams; they must outlive the cursor.
class MergeCursor {
public:
    // Yields events in (seedMicros, cutoffMicros]. Events outside the window are not returned but
    // are counted, so a caller can prove every input landed somewhere.
    MergeCursor(std::span<const CapturedOrderEvent> orderEvents,
                std::span<const CapturedTradeEvent> tradeEvents, std::uint64_t seedMicros,
                std::uint64_t cutoffMicros);

    // std::nullopt means the window is exhausted.
    [[nodiscard]] std::optional<MergedPick> next();

    // Pre-seed events are already represented by the seed snapshot, so they are skipped. They are
    // exposed as a count because a caller may still need to walk that prefix — `Replay` trains a
    // stateful classifier on it.
    std::size_t ordersBeforeSeed() const noexcept { return ordersBeforeSeed_; }
    std::size_t tradesBeforeSeed() const noexcept { return tradesBeforeSeed_; }

    // Meaningful once next() has returned std::nullopt.
    std::size_t ordersAfterCutoff() const noexcept;
    std::size_t tradesAfterCutoff() const noexcept;

private:
    bool orderInWindow() const noexcept;
    bool tradeInWindow() const noexcept;

    std::span<const CapturedOrderEvent> orderEvents_;
    std::span<const CapturedTradeEvent> tradeEvents_;
    std::uint64_t cutoffMicros_{};
    std::size_t orderPtr_{};
    std::size_t tradePtr_{};
    std::size_t ordersBeforeSeed_{};
    std::size_t tradesBeforeSeed_{};
};

}  // namespace te
