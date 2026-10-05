#include <te/engine/portfolio.hpp>

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace te {

namespace {
// __int128 is a GCC/Clang extension; __extension__ keeps -Wpedantic -Werror quiet on GCC.
__extension__ using Int128 = __int128;

// Overflow-checked int64 arithmetic (rule 11). False when the true result does not fit, in either
// direction; the caller then refuses the fill, so a wrapped value never reaches the account.
bool addFits(std::int64_t a, std::int64_t b, std::int64_t& out) {
    return !__builtin_add_overflow(a, b, &out);
}
bool subFits(std::int64_t a, std::int64_t b, std::int64_t& out) {
    return !__builtin_sub_overflow(a, b, &out);
}

Result<FillOutcome, FillError> refuse(FillError error) {
    return Result<FillOutcome, FillError>::failure(error);
}
}  // namespace

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
    const bool openingOrIncreasingShort = fill.side == Side::sell && position_.units <= 0;
    // Selling exactly the position closes it. Selling more would reverse into a short, which
    // stays unsupported until reversal is written.
    const bool reducingLong = fill.side == Side::sell && position_.units > 0 &&
                              fill.quantity.units <= position_.units;

    if (openingOrIncreasingLong) {
        // The fee is money that left, and it is also part of what acquiring the position cost, so
        // it appears in both lines on purpose (rule 4). realizedDelta stays zero: nothing closed.
        std::int64_t acquisitionCost{};
        if (!addFits(fill.notional.units, fill.fee.units, acquisitionCost) ||
            !subFits(candidateCash.units, acquisitionCost, candidateCash.units)) {
            return refuse(FillError::cash_overflow);
        }
        if (!addFits(candidatePosition.units, fill.quantity.units, candidatePosition.units)) {
            return refuse(FillError::position_overflow);
        }
        if (!addFits(candidateBasis.units, acquisitionCost, candidateBasis.units)) {
            return refuse(FillError::basis_overflow);
        }
        if (!addFits(candidateFees.units, fill.fee.units, candidateFees.units)) {
            return refuse(FillError::fee_overflow);
        }
    } else if (reducingLong) {
        // This first closing case is deliberately limited to an exactly divisible basis split.
        // Do not silently choose a remainder rule before D2 settles that policy.
        // basis x sold can exceed int64 at real magnitudes, so it is formed in 128 bits (rule 11).
        // The quotient is a fraction of basis (sold <= held), so it always fits back in int64.
        const Int128 basisTimesSold = static_cast<Int128>(basis_.units) * fill.quantity.units;
        if (basisTimesSold % position_.units != 0) {
            return Result<FillOutcome, FillError>::failure(FillError::unsupported_transition);
        }

        const auto basisLeaving =
            static_cast<std::int64_t>(basisTimesSold / position_.units);
        std::int64_t netProceeds{};
        if (!subFits(fill.notional.units, fill.fee.units, netProceeds) ||
            !addFits(candidateCash.units, netProceeds, candidateCash.units)) {
            return refuse(FillError::cash_overflow);
        }
        if (!subFits(netProceeds, basisLeaving, realizedDelta.units) ||
            !addFits(candidateRealized.units, realizedDelta.units, candidateRealized.units)) {
            return refuse(FillError::realized_overflow);
        }
        if (!addFits(candidateFees.units, fill.fee.units, candidateFees.units)) {
            return refuse(FillError::fee_overflow);
        }
        // These two only shrink toward zero: 0 < sold <= held, and 0 <= leaving <= basis.
        candidatePosition.units -= fill.quantity.units;
        candidateBasis.units -= basisLeaving;
    } else if (openingOrIncreasingShort) {
        // The mirror of opening a long. A short receives money, so the fee makes it receive less:
        // basis grows by the proceeds after the fee, not before (rule 4). Basis is a size and stays
        // positive; the short direction lives in the negative position (rule 2). Nothing closed,
        // so realizedDelta stays zero (rule 8).
        std::int64_t netProceeds{};
        if (!subFits(fill.notional.units, fill.fee.units, netProceeds) ||
            !addFits(candidateCash.units, netProceeds, candidateCash.units)) {
            return refuse(FillError::cash_overflow);
        }
        if (!subFits(candidatePosition.units, fill.quantity.units, candidatePosition.units)) {
            return refuse(FillError::position_overflow);
        }
        if (!addFits(candidateBasis.units, netProceeds, candidateBasis.units)) {
            return refuse(FillError::basis_overflow);
        }
        if (!addFits(candidateFees.units, fill.fee.units, candidateFees.units)) {
            return refuse(FillError::fee_overflow);
        }
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
