# ADR 0004: int64 ticks for price and PnL

- **Status:** accepted
- **Date:** 2026-08-09
- **Slice:** 1

## Context

The plan's first guardrail (§9). Needs an actual written argument, not an assertion.
See learncpp 4.6, 4.8, 6.7.

**Evidence added 2026-08-08**, from the Bitstamp capture. Two findings turn this from a
theoretical preference into a concrete decision with a concrete trap.

**The scales are known.** Every price in the snapshot carries exactly two decimals
(`"65015.53"`) and every size exactly eight (`"0.15253549"`). So the candidate scaling is
1e-2 for price and 1e-8 for size, both fitting `int64` with enormous headroom. This is
observed, not assumed — but it is observed on one instrument on one venue, and a second
instrument or venue may not share it, which is what makes tick size a per-instrument
property rather than a global constant.

**The venue hands you a loaded gun.** Bitstamp sends every numeric field twice:

```
"amount": 0.23904729,  "amount_str": "0.23904729",
"price":  65168.69,    "price_str":  "65168.69",
```

The bare `price` and `amount` are JSON numbers. Reading them through a binary floating-point
API would put floating point directly on the price path — the exact failure §9 warns about —
without announcing itself. It can surface later as failed equality, incorrect price-level
lookup or accumulated PnL drift, when the cause is many layers away from the symptom.

This means the decision here is not only "what type do we store" but "how do we make the
wrong field impossible to read by accident." A convention that has to be remembered every
time someone touches the decoder is not a guardrail.

## Options considered

1. **double** — what every tutorial does. Fast, familiar, and unable to represent 65168.69
   exactly, so equality comparisons and accumulated sums are both unsound.
2. **int64 ticks with a per-instrument tick size** — exact, needs a scaling decision and a
   place to keep tick size per instrument. Requires parsing the decimal string straight to
   integer rather than via `double` and multiplying, or the error is reintroduced at the door.
3. **Decimal / fixed-point library** — exact, handles scaling for you, heavier, another
   dependency to justify against [ADR 0001](0001-dependency-management.md).

## Decision

Represent prices as signed 64-bit integer ticks and quantities as signed 64-bit integer
units. The scale is not global: an `InstrumentSpec` supplied to the decoder defines the
price tick size and quantity increment for each venue/instrument pair. For the observed
Bitstamp BTC/USD schema, one price tick is `0.01 USD` and one quantity unit is
`0.00000001 BTC`.

The Bitstamp decoder must read `price_str`, `amount_str`, `amount_at_create` and
`amount_traded` as decimal text and convert them directly to scaled integers. It must not
read the JSON floating-point `price` or `amount` fields and must not pass through `double`
at any stage. The decimal parser rejects malformed input, excess fractional precision,
negative quantities and values that overflow the destination type.

Price, quantity and money are distinct strong types rather than interchangeable aliases
for `std::int64_t`. Multiplication uses a wider checked intermediate (at least 128 bits on
the supported Clang/GCC toolchains) before rescaling into the required money or PnL unit.

## Consequences

- Equality, ordering, aggregation and subtraction are exact in the engine's units.
- Instrument metadata becomes a required decoder dependency; adding another instrument
  requires an explicit scale rather than silently inheriting BTC/USD assumptions.
- A small exact-decimal parser and its error cases must be tested. The decoder test suite
  includes known conversions such as `"65168.69" -> Price{6'516'869}` and values near
  overflow and precision boundaries.
- Strong types prevent accidentally adding a quantity to a price, but require explicit
  conversion and arithmetic helpers.
- Products of two 64-bit scaled values can overflow 64 bits even when both operands fit,
  so checked wide intermediates are mandatory for notional and PnL calculations.
- If a venue reports a price that is not a multiple of its declared tick size, decoding
  fails rather than rounding it.

## How you would defend this in an interview

Binary floating point cannot exactly represent most decimal fractions, including cents,
so equality, price-level lookup and accumulated PnL can drift. I parse the venue's decimal
strings directly into per-instrument integer units, avoiding a floating-point round trip at
the boundary. Strong types and checked wide intermediates then make unit mistakes and
overflow visible instead of allowing plausible but incorrect financial results.
