# Status handoff

**Source review baseline:** 18 September 2026, `HEAD 6c2f2f6`.
The checkout was clean before documentation alignment. This update changes documentation
and current PDF exports only; it does not implement review findings or rerun the engine suite.

- [TODO.md](../../TODO.md): single active sequence, task status and completion criteria.
- [Plan v4](../project-plan-v4.md): long-range scope and evidence gates.
- [Progress guide](../project-progress-guide.md): explanation, learning map and estimates.
- [CONTEXT.md](../../CONTEXT.md): domain vocabulary.
- [CLAUDE.md](../../CLAUDE.md): working agreement.

## Current position

Entering **Stage 5**, starting with accounting examples and Portfolio.
Capture/reconstruction foundations exist; the complete trading engine does not.

ADR 0014 was accepted on 15 September. Its 18 September correction preserves accepted
authority/order decisions while identifying D5's incomplete signed-position accounting policy.
Do not restart D1-D8 wholesale or treat a documentation amendment as implemented behavior.
ADR 0015 remains accepted and explicitly interim.

The original foundation repair batch closed on 16 September. New hardening findings are
separate TODO section C tasks. In-memory accounting can proceed; trusted capture integration,
tape use and exception recovery each have their own prerequisites.

## Working agreement

Fuaad chooses domain rules and writes the first learning-critical implementation.
The assistant writes **all tests**, including tests for new features, from agreed examples,
and explains their purpose. Test authorship must not select undecided policy.
Requested reviews, bug fixes, documentation, CI and mechanical work may be handled directly.
Do not stage, commit, push or discard unrelated work without an explicit request.

## Implemented source baseline

- Exact price/quantity wrappers, decimal/integer parsing, instrument metadata, injected clock,
  byte primitives and portable v3 event/header/segment codecs.
- Standard-list-owned orders, move-only sparse reference book and ID-to-iterator index.
  Ordinary rejected events preserve state.
- Debug structural checks, separate market shape and independent `BookHealth` state machine.
  Full structural checks are not an always-on Release guarantee.
- Python joined capture/validation, manifest parsing and per-segment loading.
- `captureCoordinator` loops over all manifest segments, validates bytes/hashes/counts,
  replays from each seed and compares available aggregate checkpoints.
- `Replay` checks per-stream timestamp order and uses `MergeCursor`: venue time,
  orders before trades on exact ties.
- Zero-price lifecycle classification, trade-credit reconciliation and input accounting.
- Applied-event and aggregate-book fingerprints; mandatory synthetic joined fixture.
- GCC/Clang Debug sanitizer CI, Release job, Python discovery and hashed C++ downloads.
- The built `tep` application is the legacy recorder; the replay application is not wired.

Portfolio, strategy, decision gate, simulated venue, complete engine, queue research,
optimized structures, SPSC, live-paper operation and dashboard remain unimplemented.

## Limitations the next implementation must respect

1. **Accounting:** earlier D5 buy/sell wording was long-only. Basis, rounding, fees and
   reversals still need signed-position examples. Price times quantity requires unit conversion.
2. **Admission:** C++ validation omits `chain_valid` and `status`; Python/C++ semantic
   admission agreement remains open.
3. **Tape preconditions:** the writer does not validate ordering/window consistency.
4. **Availability:** raw files preserve receipt metadata; current envelopes/v3 records omit
   required inputs for recorded availability.
5. **Tape equivalence:** complete tape replay is absent; pre-seed classifier warm-up and
   source lineage need proof before raw/tape substitution.
6. **L3 evidence:** the aggregate digest excludes order identity and priority.
7. **CI:** UBSan recovery is not explicitly disabled; Python 3.9 is EOL.
   The float guard does not yet protect future accounting implementation files.
8. **Allocation:** `apply()` does not enforce process termination locally. A future catching
   wrapper must not retain/reuse a partially mutated book; read ADR 0015.
9. **Diagnostics:** the coordinator collapses several detailed failure causes.
10. **Corpus limits:** correction logic has synthetic evidence but did not fire in the earlier
    analyzed joined sample. Three legacy order-only adjustments remain unexplained.

Receipt-minus-venue time includes unknown clock offset. Recorded timestamps are deterministic
inputs; positive differences and a median do not establish calibrated network latency.

## Historical validation and data protection

No fresh build/test result is claimed for this documentation update.
Earlier records report 325-case GCC/Clang/configuration runs and a later 327-case local run
on 16 September. Joined real-corpus replay previously reported 0/4,533 level residuals.
These dated observations do not establish every current path.

The mandatory committed fixture is synthetic. Private corpus tests may skip when data is
unavailable and must not be the sole public correctness gate.

The user reports backup complete; an independent byte comparison was not performed.
Previous recovery checks found no remaining `dataless` flags. Do not reblock accounting
on the same backup question. Non-synced build output remains a workflow task;
the earlier damaged build folders were already repaired.

## Document/export ownership

`TODO.pdf` is the current checklist export; `new-todo-list.pdf` is the historical input.
`output/pdf/trading-engine-progress-guide.pdf` exports the current guide. The output directory
is gitignored, so a local export is not automatically available in a fresh checkout.
Older plan/learning/deep-dive PDFs are historical snapshots, not current status authorities.

## Verification when source changes

Inspect Git first. Use the chosen non-synced build path, actual CI compilers/flags,
a clean build when citing results, mandatory fixtures, appropriate sanitizers and
`git diff --check`. Check task-specific gates before marking work complete.
