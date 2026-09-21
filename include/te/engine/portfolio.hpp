#pragma once

// Exact-integer fill accounting. Rules are ADR 0014 D5; this header only declares them.
//
// Not here on purpose: no mark price, no wall clock, no fee schedule. A fill is priced by the
// venue and arrives with its fee already decided (D5 rule 10 and the D2 interface decision), so
// Portfolio records what happened rather than predicting it. Unrealized PnL needs a mark and is
// therefore a separate query, never part of applying a fill.

#include <cstdint>
#include <unordered_set>

#include <te/core/instrument.hpp>
#include <te/core/types.hpp>

#include <te/core/result.hpp>

namespace te {

// The venue's own identifier for one execution. Not a locally generated counter: a local counter
// restarts or drifts across exactly the reconnect that produces duplicates (D5 rule 10).
//
// Modelled on OrderId as a 64-bit value because the Stage 5 simulated venue issues numeric
// execution IDs. A live venue using string or UUID execution IDs would need this type widened
// before it could be used unchanged.
struct ExecutionId {
    std::uint64_t value{};

    friend constexpr bool operator==(const ExecutionId&, const ExecutionId&) = default;
};

struct ExecutionIdHash {
    std::size_t operator()(const ExecutionId& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

// One confirmed execution. Quantity is a positive magnitude with a separate side, matching
// OrderEvent; the signed position lives inside Portfolio so exactly one place can get the sign
// wrong rather than every caller.
//
// notional and fee both arrive from the venue rather than being recomputed here. FIX carries the
// same two facts in an ExecutionReport -- GrossTradeAmt(381) and the commission that makes up
// NetMoney(118) -- for the reason that decided it: the venue has already rounded price x quantity
// its own way, and recomputing it with a different rule drifts the ledger away from the actual
// account balance. price is retained as a record of the execution, not as an input to arithmetic.
struct Fill {
    ExecutionId execution_id{};
    Side side{};
    Price price{};
    Qty quantity{};
    Money notional{};
    Money fee{};
};

// Why a fill was refused. Every value leaves the account completely unchanged (D5 rule 11).
// A duplicate is deliberately absent: redelivery after a reconnect is normal protocol behaviour,
// not an error, so it is reported on the success side below.
enum class FillError {
    invalid_quantity,     // not strictly positive
    invalid_price,        // not strictly positive
    invalid_side,
    notional_overflow,    // price x quantity does not fit after rescaling
    cash_overflow,
    basis_overflow,
    realized_overflow,
    fee_overflow,
};

// What applying a fill did. Carries the realized change because that is the number a fill journal
// and every report actually wants, and the alternative is each caller reading realized before and
// after and subtracting.
struct FillOutcome {
    // False when execution_id had already been applied. The account is untouched and
    // realizedDelta is zero; this is a success, not a failure.
    bool applied{};

    // Signed change in realized PnL caused by this fill. Zero when the fill only opened or
    // increased a position, since realized arises solely from closing (D5 rule 8).
    Money realizedDelta{};

    // True when this fill crossed through flat: it closed the existing position and opened one in
    // the opposite direction. Its fee is split across both parts in proportion to quantity
    // (D5 rule 6).
    bool reversed{};
};

// Signed-position fill accounting for one instrument.
//
// Implementation contract, from D5:
//   - classify against the existing signed position, never against side alone (rule 5);
//   - a fee always moves basis against the trader: onto a long's cost, off a short's
//     proceeds (rule 4);
//   - realized is net of fees and only ever produced by closing (rules 3 and 8);
//   - partial closes subtract what left and never rebuild basis from a rounded average
//     (rule 12);
//   - position is zero if and only if basis is zero (rule 7);
//   - compute the whole candidate, validate it, then commit in one step, so a rejected fill
//     cannot half-change the account (rule 11).
class Portfolio {
public:
    // The spec is needed only by unrealizedAt: a mark is a price nobody has traded at, so no
    // venue supplies its notional and this is the one place Portfolio must convert price ticks
    // and quantity units into money itself. The fill path never converts.
    explicit Portfolio(InstrumentSpec spec) : spec_(spec) {}

    Result<FillOutcome, FillError> applyFill(const Fill& fill);

    Money cash() const { return cash_; }

    // Signed: positive is long, negative is short.
    Qty position() const { return position_; }

    // Total for the whole open position, not a per-unit average. What was paid for a long, what
    // was received for a short. The average is derived by dividing by position and is
    // deliberately never stored (rule 2).
    Money basis() const { return basis_; }

    Money realized() const { return realized_; }

    Money feesPaid() const { return feesPaid_; }

    // Requires a mark because paper profit depends on a price nobody has traded at. Kept off the
    // fill path so an account cannot silently value itself at whatever price happened to arrive
    // last (D5, unrealized paragraph). Converts mark x position through a 128-bit intermediate
    // (ADR 0004) and rounds against the trader when the rescale does not divide evenly, matching
    // the conservative asymmetry venues use for position valuation.
    Money unrealizedAt(Price mark) const;

private:
    InstrumentSpec spec_{};
    Money cash_{};
    Qty position_{};
    Money basis_{};
    Money realized_{};
    Money feesPaid_{};

    // Every execution applied so far. Retained for the whole run rather than windowed: replay
    // inputs are finite capture files, so this has a natural bound, and a duplicate can arrive at
    // any distance behind the original (D5 rule 10). Unbounded growth only becomes real under
    // continuous live operation, which is Stage 9's problem.
    std::unordered_set<ExecutionId, ExecutionIdHash> appliedExecutions_;
};

}  // namespace te
