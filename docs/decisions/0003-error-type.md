# ADR 0003: Error representation in the hot path

- **Status:** accepted
- **Date:** 2026-08-08
- **Slice:** 0

## Context

Follows from ADR 0002. If the hot path does not throw, submit() and try_pop() need a way to say no.

## Options considered

1. **std::optional** (learncpp 12.15) — no reason, just absence.
2. **Result<T, Error>** — carries a reason, more code to write.
3. **bool return plus out-parameter** — what the SPSC queue signature already implies.

## Decision

Use the smallest error contract that preserves the information a caller needs:

- Use `bool` plus an out-parameter for a single obvious binary outcome. For example,
  `try_pop(T& out) noexcept` returns `false` when the queue is empty.
- Use `std::optional<T>` when a value may simply be absent and the reason is not needed.
- Use `Result<T, Error>` when several failures are possible and callers need a reason
  for logging, recovery or diagnosis. Decoding and record I/O fall into this category.
- Exceptions may be used for unexpected failures at cold boundaries, in accordance
  with ADR 0002.

## Consequences

- Queue and similar hot-path APIs remain small, allocation-free and unambiguous.
- Parsing and I/O retain enough error information to distinguish malformed input,
  unsupported schemas and operational failures.
- Callers must handle failure explicitly instead of relying on stack unwinding.
- The project has more than one error representation, so interfaces must document why
  their selected representation matches the operation's failure modes.
- `std::optional` must not be used when losing the failure reason would prevent correct
  recovery or useful telemetry.

## How you would defend this in an interview

One error type is not optimal for every operation. The SPSC queue uses `bool` because
empty or full is already unambiguous and the API is latency sensitive. A decoder uses
`Result<T, Error>` because callers must distinguish missing fields, invalid values and
unsupported schema versions. `std::optional` is reserved for reasonless absence.
