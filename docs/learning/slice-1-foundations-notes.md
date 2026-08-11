# Slice 1 foundations: revision notes

**Snapshot:** 2026-08-10  
**Purpose:** Explain the C++ foundations currently implemented in the repository, why the
designs exist in a trading engine, and how the matching tests prove their behaviour.

This is deliberately a learning document, not a substitute for the source. Read it beside the
files named in each section. A `TODO(fuaad)` file is a planned component, not completed code.

## 1. Current status at a glance

| Area | Code status | Test status | Why it exists |
| --- | --- | --- | --- |
| Strong market types | implemented | passing | Prevent price, quantity and order-ID mix-ups. |
| Normalized `OrderEvent` | implemented | passing | One venue-independent representation of an L3 order event. |
| `InstrumentSpec` | implemented | passing | Holds per-market decimal scales; no hidden BTC/USD defaults. |
| `Result<T, E>` | implemented | passing | Return an exact value *or* a reasoned failure without exceptions. |
| Exact decimal parser | interface/error enum only | not registered or tested yet | Convert venue decimal text into integer ticks safely. |
| Record layout, byte buffer, decoder | placeholder only | placeholder only | Next Slice 1 work after decimal parsing. |
| Book, replay, simulator, strategy, live adapter | placeholder only | placeholder only | Later slices. |

**Do not infer implementation from a filename.** A header containing only a `TODO` comment is an
intentional placeholder and has no behaviour yet.

## 2. Repository map and dependency direction

```text
include/te/core/      small reusable, venue-independent value concepts
include/te/feed/      converts venue data into normalized engine events
include/te/telemetry/ binary capture records and outputs
include/te/book/      L3 order-book state (Slice 2)
include/te/engine/    event loop, strategy and portfolio (Slice 3)
include/te/venue/     simulated and later external execution (Slices 3–5)
include/te/util/      data structures and low-level helpers
tests/unit/           fast tests for one small behaviour each
tests/golden/         future recorded-input / expected-output regression tests
```

The desired direction is inward:

```text
raw venue JSON -> feed decoder -> core types / OrderEvent -> book -> engine -> venue
```

`core` must not depend on a specific exchange, GUI, websocket, or strategy. That keeps the
highest-value correctness code easy to reuse and test.

## 3. C++ header basics used throughout

### `#pragma once`

Every current header begins with `#pragma once`. It prevents the same header being included more
than once in a single translation unit. Without an include guard, repeated definitions could cause
compiler errors.

### `namespace te`

All engine code currently lives in namespace `te` (Trading Engine):

```cpp
te::Price
te::Qty
te::Result<int, SomeError>
```

Namespaces prevent collisions with names from the standard library or another dependency.

### Header versus source file

Headers expose declarations and small inline/template definitions. A template normally has its
definition in the header because the compiler must generate a concrete type where it is used.

For example, compiling `Result<int, TestError>` requires the compiler to see all `Result<T, E>`
function bodies in `result.hpp`. A generic template body hidden in a later `.cpp` file would not be
available to generate that specialization.

## 4. Strong market types: `core/types.hpp`

Source: `include/te/core/types.hpp`  
Tests: `tests/unit/test_types.cpp`

### The problem

Price and quantity are both naturally represented with integers. An alias does not protect us:

```cpp
using Price = std::int64_t;
using Qty = std::int64_t;
```

With aliases, the compiler treats both names as the same type. Accidentally passing quantity where
price belongs compiles silently—the most dangerous kind of error in market code.

### The solution: tiny wrapper structs

`Price`, `Qty`, and `OrderId` are distinct structs containing their underlying values:

| Type | Stored field | Meaning |
| --- | --- | --- |
| `Price` | signed `int64_t ticks` | Exact price in an instrument’s smallest stored unit. |
| `Qty` | signed `int64_t units` | Exact quantity in the instrument’s stored quantity unit. |
| `OrderId` | unsigned `uint64_t value` | Venue order identifier, not arithmetic quantity. |
| `Side` | `uint8_t` enum | `buy` or `sell`. |

The `DistinctTypesDoNotInterconvert` test uses compile-time assertions to prove `Qty` cannot become
`Price`, and `Price` cannot silently become an integer. This is a **design contract**, not runtime
behaviour.

### Exact integer pricing

For Bitstamp BTC/USD, two price decimal places mean:

```text
"65168.69" -> Price{6'516'869}
```

The integer is exact. Later arithmetic adds integer ticks, which cannot accumulate binary
floating-point rounding error. The price scale is not stored in every `Price`; it belongs to the
venue/instrument metadata in `InstrumentSpec`.

`PriceArithmetic.TickSumsAreExact` demonstrates why production boundary code must never decode a
price through `double`. The use of `double` in that test is a demonstration of the failure mode,
not a production technique.

### `enum class Side : std::uint8_t`

`enum class` is a scoped enum. Calling code must write `te::Side::buy`, rather than a loose global
`buy`. Its explicit one-byte underlying type makes the intended in-memory representation clear.

### Comparisons and `friend`

`Price` has defaulted equality and three-way comparison:

- `operator==` makes `first == second` compare their `ticks` members.
- `operator<=>` supplies an ordering, so `first < second`, `first <= second`, and related
  comparisons work.
- `= default` asks the compiler to generate memberwise comparison. That works for equality and
  three-way comparison, but not for a standalone `<` operator.

These are hidden non-member friends. A friend is not a member function; it receives both operands
symmetrically. It could access private representation if `ticks` later becomes private. It is
currently not required for access because the members are public, but preserves a clean comparison
shape.

### Compile-time contracts: `static_assert`

`Price`, `Qty`, `OrderId`, and `Side` have `std::is_trivially_copyable_v` assertions in their
defining header. This is intentional:

```text
Permanent representation requirement -> static_assert beside the type
Specific runtime result                -> unit test
```

Trivially copyable means the object has simple value-like lifetime/copy rules. It is an important
precondition for later fixed-capacity queues and byte-oriented records. It does **not** by itself
define a portable binary file format; field order, size, alignment, versioning, and endianness still
need an explicit record design.

## 5. Normalized L3 event: `feed/events.hpp`

Source: `include/te/feed/events.hpp`  
Tests: `tests/unit/test_events.cpp`

The decoder will translate venue-specific JSON into `OrderEvent`. The book should consume this
normalized type rather than learn Bitstamp field names and JSON syntax.

| Field | Meaning |
| --- | --- |
| `venue_timestamp_us` | Exchange timestamp in microseconds. |
| `order_id` | Which resting order changed. |
| `price` | Exact integer-tick price. |
| `quantity` | Venue-reported remaining/inserted quantity. |
| `side` | Buy or sell. |
| `kind` | `add`, `modify`, or `remove`. |

### Quantity contract

This is important enough to be written beside the field:

| Event kind | Meaning of `quantity` |
| --- | --- |
| `add` | Quantity inserted into the book. |
| `modify` | New remaining quantity; it replaces the prior quantity. |
| `remove` | Reported deletion quantity, retained for audit only. Remove the order by ID; do not subtract this field from the price level. |

The contract prevents a subtle accounting bug: deletion messages may report quantity that is not
safe to treat as a book delta.

`EventKind` has a one-byte underlying-type contract. `OrderEvent` is checked as trivially
copyable **after** its closing brace, because type traits require the complete type.

The test uses C++20 designated initialisers (`.price = ...`) to make a constructed event readable
and prevent field-order mistakes.

## 6. Instrument metadata: `core/instrument.hpp`

Source: `include/te/core/instrument.hpp`  
Tests: `tests/unit/test_instrument.cpp`

An `InstrumentSpec` identifies one venue/market pair and its text scales:

```text
Bitstamp BTC/USD: price_decimals = 2, quantity_decimals = 8
```

The test with a synthetic 4/3 specification proves scales are not global constants. It does not
claim a real venue uses that specification.

### Scale is not tick-size policy

A decimal scale tells the parser how many fractional digits to convert into an integer unit:

```text
scale 2: "1.2" -> 120
scale 8: "1.2" -> 120'000'000
```

It does not necessarily describe all venue order constraints. A venue may accept four fractional
digits but permit orders only in increments of `0.0005`, and may also specify a minimum quantity or
minimum notional. Those are separate rules to add only when verified from venue metadata.

## 7. Reasoned success/failure: `core/result.hpp`

Source: `include/te/core/result.hpp`  
Tests: `tests/unit/test_result.cpp`  
Decision: ADR 0003

### Why this type exists

`std::optional<T>` can say “there is no result,” but does not explain why. Decimal parsing needs to
distinguish empty input, invalid character, excessive precision, and overflow. `Result<T, E>` holds
either a success `T` or an error `E`.

```text
Result<int64_t, ParseError>
    success -> exact parsed tick count
    failure -> ParseError explaining why parsing failed
```

### Templates

In this declaration:

```cpp
template <typename T, typename E>
class Result;
```

`T` and `E` are type parameters. The compiler creates a concrete specialization when used:

```text
Result<int, TestError>
Result<int64_t, ParseError>
Result<OrderEvent, DecodeError>
```

The current type is generic over the success and error types, but it is deliberately small rather
than a full replacement for a standard-library `expected` type.

### Storage and factories

`std::variant<T, E> storage_` holds exactly one alternative. Its public factories make the intent
clear:

| Call | Active value in `storage_` |
| --- | --- |
| `Result<T, E>::success(value)` | a `T` |
| `Result<T, E>::failure(error)` | an `E` |

The constructors are private and `explicit`. Outside code must say `success(...)` or
`failure(...)`; it cannot accidentally turn a raw integer or error enum into a `Result`.

The constructor member-initialiser list constructs `storage_` directly. This matters because
assigning in the constructor body would first require a default-constructed `T`, a requirement a
future successful type may not satisfy.

`std::move` does not itself move data. It permits the receiving object to move from a local
parameter that will not be used again. This avoids an unnecessary copy for a potentially large
future success value.

### Query API

| Function | Result |
| --- | --- |
| `hasValue()` | `true` when the variant currently holds `T`. |
| `valueIf()` | Pointer to `const T`, or `nullptr` if failure. |
| `errorIf()` | Pointer to `const E`, or `nullptr` if success. |

`std::get_if` matches this API because it returns a pointer or `nullptr`; `std::get` would assume
the alternative exists and throw on the wrong alternative.

The current type-based `std::holds_alternative<T>` and `std::get_if<T>` interface assumes `T` and
`E` are different types. That is true for all intended uses (`int64_t` versus `ParseError`, for
example). If a future caller needs identical alternatives, the design must use tagged/indexed
storage rather than this form.

### Tests

The tests cover both states:

- success has a value, value pointer is non-null, correct value is observed, error pointer is null;
- failure has no value, value pointer is null, error pointer is non-null, correct error is observed.

`ASSERT_NE(pointer, nullptr)` is deliberately used before `*pointer`: `ASSERT_*` stops the current
test on failure and prevents dereferencing null. `EXPECT_*` records a failure but continues the
test, which is appropriate only when later operations remain safe.

## 8. Test and build infrastructure

Source: `tests/CMakeLists.txt`

Each registered unit test follows this path:

```text
test source -> test executable -> links te_core + GoogleTest main -> GoogleTest discovers cases -> CTest runs them
```

`te_add_test(name source)` centralises the CMake policy:

- creates an executable from one test source;
- links `te_core` and `GTest::gtest_main`;
- applies project warnings and sanitizers;
- asks GoogleTest/CMake to discover individual `TEST(Suite, Name)` cases.

The currently registered test targets are `test_types`, `test_events`, `test_instrument`, and
`test_result`. `test_decimal.cpp` exists but is deliberately **not registered yet**; it is an empty
placeholder and does not prove that `text_to_int.hpp` compiles.

Useful commands from the project root:

```sh
cmake -S . -B build
cmake --build build --target test_result
ctest --test-dir build --output-on-failure -R '^Result\.'
ctest --test-dir build --output-on-failure
```

The final command runs every discovered test. The `-R` form filters by the GoogleTest suite name.

## 9. Decimal parser: current state and exact next boundary

Source: `include/te/core/text_to_int.hpp`  
Test placeholder: `tests/unit/test_decimal.cpp`

`ParseError` is a good initial error vocabulary:

| Error | Meaning |
| --- | --- |
| `empty_input` | No text was supplied. |
| `invalid_character` | A character other than a digit/accepted separator/sign was found. |
| `invalid_format` | Text uses an invalid structure, such as multiple decimal points. |
| `negative_not_allowed` | The boundary rejects negative text. |
| `excess_precision` | More fractional digits than the supplied scale permits. |
| `unsupported_scale` | Scale lies outside the deliberately supported range. |
| `overflow` | Exact integer result cannot fit in `int64_t`. |

The current `DecimalParsing()` placeholder has no parameters, returns a raw integer, and has no
useful body. It is not yet a parser contract and is not compiled by a registered test target.

The next interface should be a **free function** in `te` with this English signature:

```text
parse decimal text plus a decimal scale
returns Result<int64_t, ParseError>
```

The first tests should use real captured values and precise expected ticks:

```text
"58356.10", scale 2      -> 5'835'610
"0.00171371", scale 8    -> 171'371
"1.2", scale 2           -> 120
"1.234", scale 2         -> excess_precision
```

No parser implementation should use `double`, `std::stod`, or rounding.

## 10. Placeholder file map: later, not today

| Directory | Later responsibility |
| --- | --- |
| `core/time.hpp` | Clock abstraction; centralizes real system-clock access. |
| `util/byte_buffer.hpp`, `telemetry/record.hpp`, `telemetry/sink.hpp` | Versioned fixed-size capture format and append-only output. |
| `feed/bitstamp_decoder.hpp` | Renamed from `coinbase_decoder.hpp` to reflect ADR 0010 (Bitstamp is the primary L3 venue). Planned role is the decoder boundary: JSON to `OrderEvent`. |
| `book/*` | L3 order book and FIFO price levels, Slice 2. |
| `engine/*`, `feed/feed.hpp`, `feed/replay_feed.hpp`, `venue/simulated_venue.hpp`, `venue/venue.hpp` | Offline replay and simulated execution, Slice 3. |
| `venue/queue_model.hpp`, `venue/latency_model.hpp` | Queue/fill model, Slice 4. |
| `feed/live_feed.hpp`, `venue/coinbase_venue.hpp`, `telemetry/sink.hpp` | Later live observation/telemetry boundaries. Current plan treats external execution as optional. |
| `util/memory_pool.hpp`, `util/intrusive_list.hpp`, `util/spsc_queue.hpp` | Performance-oriented structures introduced only once profiling/design needs justify them. |

## 11. Tomorrow’s resume checklist

1. Re-read Sections 4, 6, and 7 beside their headers and tests.
2. In `text_to_int.hpp`, replace the placeholder with the parser declaration returning
   `Result<int64_t, ParseError>` and accepting text plus scale.
3. Register `test_decimal` only after it contains a meaningful red test.
4. Write one success test from real Bitstamp text and one failure test before writing a digit loop.
5. Build the one target, then run the filtered CTest command.
6. Do not begin the JSON decoder until exact decimal parsing, overflow checks, and error cases are
   green.

## 12. Quick self-check

1. Why are `Price` and `Qty` structs instead of `using` aliases for `int64_t`?
2. What does two-decimal scale do to the text `"1.2"`?
3. Why is a `static_assert` beside `OrderEvent` stronger than an equivalent assertion in a test?
4. In `Result<int64_t, ParseError>`, what does `valueIf()` return after `failure(...)`?
5. Why does `Result<T, E>` live fully in a header?
6. Why must `ASSERT_NE(pointer, nullptr)` occur before dereferencing a test pointer?

Answers: (1) compile-time domain safety; (2) `120` ticks; (3) it enforces a permanent contract in
every build at the defining location; (4) `nullptr`; (5) the compiler needs its definitions to
generate each specialization; (6) it stops an unsafe null dereference after failure.
