#include <te/engine/portfolio.hpp>

namespace te {

// ============================================================================================
// NOT IMPLEMENTED. These bodies are scaffolding so the header has a translation unit and the
// build stays green; the arithmetic is Fuaad's to write (TODO D2, working agreement).
//
// The rules are ADR 0014 D5, and the class contract in portfolio.hpp restates the six that bite
// hardest. Two interface questions are still open and should be settled before writing these:
// where the instrument scales come from, and which way the price x quantity conversion rounds.
//
// applyFill deliberately returns a failure rather than a plausible-looking success, so that any
// test passing against this stub is provably testing nothing.
// ============================================================================================

Result<FillOutcome, FillError> Portfolio::applyFill(const Fill& fill) {
    (void)fill;
    return Result<FillOutcome, FillError>::failure(FillError::invalid_quantity);
}

Money Portfolio::unrealizedAt(Price mark) const {
    (void)mark;
    return Money{};
}

}  // namespace te
