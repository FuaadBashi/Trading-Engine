# Research extension plan (PhD bridge)

- **Status:** proposed, 2026-09-19. Not accepted. Nothing here changes TODO or plan v4 yet.
- **Purpose:** use this engine as the software spine for PhD work on drying (physics modelling,
  machine learning, process monitoring) without damaging either half.

## The honest framing

This roughly **doubles the project**. It adds a second scientific domain to something that has not
yet finished Stage 5. That can be worth it, but it should be chosen, not drifted into.

The shared skill is real and narrow: **making decisions from noisy, delayed observations, and
proving whether the decisions helped.** Almost nothing else transfers.

| Transfers | Does not transfer |
|---|---|
| Reproducible experiments; recorded inputs, seeds, code version | Exact-integer money arithmetic (financial only) |
| Splitting evaluation by session/batch, never by timestamp | Order books, venue protocols, queue position |
| Uncertainty that is checked against observed error | Numerical solvers, dimensional analysis, physical constraints |
| Profiling, allocation, data layout, parallel runs | Floating point (required for physics, banned in money) |

Forcing more unity than this produces a bad shared abstraction. The float guard
(`scripts/check_architecture_guards.py`) already encodes the boundary: drying code needs floating
point; `Portfolio` and the book must never have it.

## The one structural rule

**One harness, two separate domains.** The only shared artifact is the experiment runner. It is a
harness, not a framework, and it knows nothing about either domain.

```
              experiment runner
        (config, seeds, logs, evaluation)
                /            \
       trading engine      drying model
      (exact integers)    (floating point)
```

Plan v4 §4 already states the C++/Python boundary: Python configures, reports and analyses; it
never reconstructs the book, computes fills, manages risk or keeps a second ledger. That rule is
unchanged — this plan applies it earlier than Stage 10, it does not rewrite it.

Extract anything else shared only after both sides demonstrate a real common need.

## Sequence

1. **Finish Stage 5.** Portfolio, order lifecycle, risk checks, scheduling, reproducible replay.
   Unchanged from TODO. Nothing below is a prerequisite for it.
2. **Build the experiment runner — and prove it on trading first.** One command reproduces a
   result from recorded data, parameters and code version. Trading is the right first user because
   real capture data already exists. Python invokes the C++ executable and reads structured
   results; add bindings only when call or transfer cost justifies them.
3. **One uncertainty study, trading.** Does an observed order fill within a horizon? Evaluate
   against a naive baseline, check whether stated uncertainty matches observed error, and split by
   session — never by neighbouring timestamps.
4. **Define the drying question.** One prediction target, the measurements actually available at
   prediction time, the operating range covered, and how the target is measured independently.
   Requires the supervisor. One page plus one example data record.
5. **Physics baseline, as a separate companion.** Simplest defensible model. Explicit units,
   parameters calibrated on training data, convergence checked as solver settings tighten.
6. **Three-model comparison.** Physics-only, data-only, physics-plus-learned-correction, on the
   same target and evaluation data, split by batch. Report where the hybrid does not help.
7. **Optimization, then possibly control.** Grid and random search before constrained Bayesian
   optimization. Model predictive control is a separate extension; prediction accuracy does not
   establish that a controller is safe.
8. **Stage 8/9 performance and recovery** across both workloads, against a correct baseline.

## Blockers and off-ramps

Steps 4 onward are **blocked on things this repository does not contain**: real experimental data
and supervisor agreement on the model. Until those exist, only steps 1-3 are actionable. Do not
build a drying simulator against imagined data.

Abandon or descope the drying half if any of these hold:

- no usable experimental dataset within the period you have allocated to it;
- the physics baseline cannot be calibrated to the available measurements;
- the trading engine stops progressing because of it.

A toy drying simulation is a learning exercise, not a digital twin. Synthetic data can verify the
software; it cannot establish accuracy on real material.

## Why hybrid is the research candidate, not the assumed answer

Hybrid physics-plus-correction is an established approach in process research, which justifies
investigating it. It does not establish superiority for these materials. The physics-only baseline
is built first because without it there is nothing to attribute an improvement to. A correction
model learned on top of mostly-measurement-noise makes predictions worse, and a correction applied
to an output does not automatically preserve mass, energy or stability — which matters if control
ever follows.
