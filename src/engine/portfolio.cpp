#include <te/engine/portfolio.hpp>

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

    // TODO(fuaad): this is the part that is yours.
    //
    // For the first test only the simplest case matters: flat account, a buy, so this fill just
    // opens a long. Closing, covering and reversing all come later -- do not write them yet.
    //
    // Four values move. Work out from D5 what each becomes:
    //   - candidateCash      what leaves the account (rule 4: the fee is real money that left)
    //   - candidatePosition  a buy adds, a sell subtracts (rule 5 works off the signed position)
    //   - candidateBasis     what this position cost you (rule 4: does the fee belong in here?)
    //   - candidateFees      the running total
    //
    // realizedDelta stays zero: opening never realizes, because nothing has been closed (rule 8).
    //
    // Note Money has no operator+ yet -- do the arithmetic on .units, e.g.
    //   candidateCash.units -= something;
    // Adding operators to Money is a reasonable follow-up, but it is a design choice, not a
    // requirement of this test.

    
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
    // NOT IMPLEMENTED. Needed by the last three tests, not the first.
    // mark x position against basis, through a 128-bit intermediate, rounded against the trader.
    (void)mark;
    return Money{};
}

}  // namespace te
