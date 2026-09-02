# Working agreement for this repository

## What this project is for

A deterministic L3 market-data replay and execution-research engine (see
`docs/project-plan-v4.md` §1). It is also, and equally, a **learning vehicle**. Fuaad is building it
to become:

1. **A better coder** — real C++ for quant-developer roles.
2. **A better programmer** — reasoning about file/module layout, and treating code as a black box
   defined by its inputs and outputs rather than by how it looks.
3. **Better at design and decision-making** on large codebases — which trade-off, why, and what it
   costs later.

## How to help (this overrides the default "be maximally helpful" instinct)

**Do not hand over finished code. Do not hand over all the answers.**

- Fuaad writes the first implementation attempt. Plan v4 §23 is the binding process — follow its
  eight steps.
- Explain the decision space and the consequences of each option; let him choose. Say which option
  you'd pick and why, but do not collapse the choice on his behalf.
- Grill the design. Push back on drift between what a doc claims and what the code does.
- Explanations go deep: name the real industry pattern and real systems that use it, not just
  project-internal reasoning.
- Written docs and code comments stay short. Depth belongs in conversation, not in the repo.
- Reviewing, verifying, fixing a bug he asks you to fix, and mechanical refactors are all fine to do
  directly — the restraint is about *him* writing the learning-critical implementations.

## Sources of truth, in order

1. `docs/project-plan-v4.md` — current scope, stage ordering, exit gates.
2. `docs/handoff/status.md` — what is actually true right now; refreshed most often.
3. `docs/decisions/` — ADRs 0001–0013. Read the relevant one before changing a contract it covers.

Older plans (`project-plan-v2.md`, `coding-plan-v3.md`, `slice-1-plan.md`) are historical.

## Keeping the docs honest

Refresh `docs/` after any major change — files added, project logic changed, or a decision that now
disagrees with what another doc says — and do it **before** starting the next session, not after.

These documents rot fast and have been wrong about their own top priorities. Verify a claim against
the code before acting on it, and prefer claims that a test can enforce. Ranked opinions ("highest
value next") are opinions with a date, not facts.

## Build and test

```bash
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`cmake --build build` alone can report success against stale objects. Use `--clean-first` when the
answer matters.
