#include <te/engine/portfolio.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace te {

Result<FillOutcome, FillError> Portfolio::applyFill(const Fill& fill) {
    // --- 1. Reject bad input before touching anything (rule 11) -------------------------------
    if (fill.quantity.units <= 0) {
        return Result<FillOutcome, FillError>::failure(FillError::invalid_quantity);
    }
    if (fill.price.ticks <= 0) {
        return Result<FillOutcome, FillError>::failure(FillError::invalid_price);
    }
    if (fill.side != Side::buy && fill.side != Side::sell) {
        return Result<FillOutcome, FillError>::failure(FillError::invalid_side);
    }

    // --- 2. Already seen? Ignore it, and say so on the success side (rule 10) ------------------
    // Checked after validation on purpose: a rejected fill must not consume its execution id,
    // because the venue may reuse it on a corrected message.
    if (appliedExecutions_.contains(fill.execution_id)) {
        return Result<FillOutcome, FillError>::success(FillOutcome{.applied = false});
    }

    // --- 3. Work out the whole new state before writing any of it (rule 11) --------------------
    // Locals, not members. Nothing above is allowed to touch this->, so an error discovered
    // halfway through leaves the account exactly as it was.
    Money candidateCash = cash_;
    Qty candidatePosition = position_;
    Money candidateBasis = basis_;
    Money candidateRealized = realized_;
    Money candidateFees = feesPaid_;
    Money realizedDelta{};

    const bool openingOrIncreasingLong = fill.side == Side::buy && position_.units >= 0;
    const bool reducingLong = fill.side == Side::sell && position_.units > 0 &&
                              fill.quantity.units < position_.units;

    if (openingOrIncreasingLong) {
        // The fee is money that left, and it is also part of what acquiring the position cost, so
        // it appears in both lines on purpose (rule 4). realizedDelta stays zero: nothing closed.
        const std::int64_t acquisitionCost = fill.notional.units + fill.fee.units;

        candidateCash.units -= acquisitionCost;
        candidatePosition.units += fill.quantity.units;
        candidateBasis.units += acquisitionCost;
        candidateFees.units += fill.fee.units;
    } else if (reducingLong) {
        // This first closing case is deliberately limited to an exactly divisible basis split.
        // General remainder policy and checked arithmetic remain separate D2 work.
        const std::int64_t basisLeaving =
            (basis_.units / position_.units) * fill.quantity.units;
        const std::int64_t netProceeds = fill.notional.units - fill.fee.units;

        realizedDelta.units = netProceeds - basisLeaving;

        candidateCash.units += netProceeds;
        candidatePosition.units -= fill.quantity.units;
        candidateBasis.units -= basisLeaving;
        candidateRealized.units += realizedDelta.units;
        candidateFees.units += fill.fee.units;
    } else {
        return Result<FillOutcome, FillError>::failure(FillError::unsupported_transition);
    }

    // --- 4. Commit everything at once ---------------------------------------------------------
    cash_ = candidateCash;
    position_ = candidatePosition;
    basis_ = candidateBasis;
    realized_ = candidateRealized;
    feesPaid_ = candidateFees;
    appliedExecutions_.insert(fill.execution_id);

    return Result<FillOutcome, FillError>::success(
        FillOutcome{.applied = true, .realizedDelta = realizedDelta, .reversed = false});
}

Money Portfolio::unrealizedAt(Price mark) const {
    // Not implemented. Aborts rather than return a zero that looks like a real answer.
    // TODO(fuaad): mark x position against basis, 128-bit intermediate, rounded against the trader.
    (void)mark;
    std::fputs("Portfolio::unrealizedAt is not implemented\n", stderr);
    std::abort();
}

}  // namespace te
