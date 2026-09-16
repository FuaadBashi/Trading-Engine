# Trading Engine — To-Do List

Updated 15 September 2026. Replaces the previous list.

> Source: the supplied [new-todo-list.pdf](TODO.pdf), imported on 15 September 2026. This replaces the previous checklist. Wording, priorities, decisions and completion claims below are the supplied document's record; this import did not execute its commands, rerun tests, commit work, or update/accept ADR 0014.

How to read this. Every job from the old list is still here. Jobs you have finished are in Done at the end. NEW means the job is not on the old list. UPDATED means the job changed because of what we found this week.

No deadlines are set. The old list had none, and you have not given me any. See Questions for me.

## Do these first

Seven jobs from the original list are now done: capture files are protected, all eight ADR 0014
questions are answered (D1-D8), **ADR 0014 is reviewed and accepted (15 September 2026)**, that work
is committed and pushed, and **the loader now checks capture completeness (16 September 2026,
commit `aef6fc3`, not yet pushed)**.

Nothing is flagged urgent right now. Section A still has two open small jobs (commit the remaining
uncommitted docs, delete the broken `build/` folder) and section C has several foundation repairs
left before section D's engine work can start — see below.

## A. Protect what you have

Small jobs. Do them first because they stop you losing work.

### Protect your capture files from being wiped

- [ ] **HIGH | NEW | NO DEADLINE**

Your project sits in ~/Desktop, which syncs to iCloud. iCloud removes the contents of big files it thinks you are not using, and leaves an empty shell behind. That is what happened to your capture data. The files looked normal but read as empty.

Your capture files are not in git (they are too big), so git cannot bring them back.

**Steps**

1. Choose one: move the whole project out of ~/Desktop, or move just the data/ folder somewhere iCloud does not sync.

2. Copy the data/ folder to a second place — an external drive or a backup service that is not iCloud.

3. Turn off "Optimise Mac Storage" in System Settings → Apple Account → iCloud, if you keep the project where it is.

4. Check nothing is still hollow.

**Commands You Will Need**

```bash
# list files iCloud has emptied out
find data -type f -flags +dataless
```

```bash
# pull one back
brctl download data/raw/<capture>/segment-0000.jsonl
```

13 files under data/ were still hollow after the last check. Biggest at risk: data/rawOld/btcusd-live-orders.jsonl (126 MB). "dataless" is the macOS flag meaning "contents are not on this laptop".

**Done when:** find data -type f -flags +dataless returns nothing, and a full copy of data/ exists somewhere outside iCloud.

### Commit the work sitting on your laptop

- [ ] **HIGH | NEW | NO DEADLINE**

There are code fixes, document updates and two new PDFs that are not saved to git.

**What Is Waiting**

- Six small code and comment fixes (listed in Done).

- Updated README.md, TODO.md, status.md and docs/project-progress-guide.md.

- docs/TradingEngine-DeepDive.pdf — the 54-page code manual.

- This list, once you are happy with it.

Suggested split: one commit for the review-driven document updates, one for the code fixes, one for the PDFs.

**Done when:** git status is clean, and the test suite still passes 304 of 304.

### Delete the broken build folder

- [ ] **LOW | NEW | NO DEADLINE**

The build/ folder in your project no longer works. It fails before it starts with FindThreads only works if either C or CXX language is enabled. Your source code is fine — only the saved build settings are broken.

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

**Done when:** cmake --build build -j works from a clean start and ctest --test-dir build passes 304 of 304.

## B. Finish the design decision — DONE, ADR 0014 accepted 15 September 2026

This was the gate. No engine code could be written until ADR 0014 was accepted; now it can, once
section C's foundation repairs are also done. An "ADR" is a short document recording one design
decision and why you made it. Kept here for the record of how the five sessions got there.

### Save this week's five answers into ADR 0014

- [x] **HIGH | NEW | NO DEADLINE** — done 15 September 2026

Written into the ADR as **D1-D5**, each with its reasoning. The open list is now three items,
renumbered. The hand-worked timeline runs to T11. Status line updated to
"proposed — 5 of 8 open questions resolved (D1-D5); 3 remain".

Two consequential edits followed from D1: the Stage 5 working scope no longer asks for a gate
combining "trust, shape and a minimal replay operational state", and the consequences section no
longer claims operational state is observable at Stage 5.

**D4's availability assumption is now settled, not just flagged.** Ordering stays venue-time-only.
Separately, measuring `localWallTimestampNanos - venueTimestampMicros` over 189,375 real order
frames and 789 trades gave a 75.1 ms median delay (33.2 ms floor, 1382.7 ms worst case) — so zero
market-data delay was not a safe silent assumption. D4 now defines three named, swappable
availability policies (`zero`, `fixed` at 80 ms, `recorded`), stamped per run, with the same
"implement more than one and report the spread" reasoning ADR 0008 already uses for cancel
assumptions. `fixed`'s 80 ms constant is a declared round number, not a claimed measurement — open
to revision.

You settled five of the eight open questions in conversation. The ADR previously listed all eight as open.

**The Five Answers To Write Down**

1. Operational state: do not build it yet. At this stage there is no operator, no kill switch, and a file cannot disconnect — so it could only ever hold one value and no test could prove it works. The decision gate checks two things for now: is the data trustworthy, and is the market a shape you can trade.

2. Latency queue: an order only joins the queue after both the decision gate and the risk check say yes. The queue models travel time to the exchange, not thinking time. A rejected order never enters it.

3. First fill: an order can fill the moment it arrives, using the book as it stands at that exact moment.

4. Time order: use the exchange's own timestamp only — never the time your machine received it, because that changes with your network speed. If a real market event and your own order share a timestamp, the real event goes first. If two of your own orders share one, the one submitted first goes first.

5. Money order: confirm the fill, work out the fee, then — when buying, add the price and fee into a new average cost; when selling, use the old average cost to work out profit, and take the fee off that profit. Update cash, position, average cost and profit together, so nothing ever sees half an update. Selling part of a holding does not change the average cost of what is left.

File: `docs/decisions/0014-event-loop-causality-and-decision-authority.md`

Status today: proposed. It becomes accepted once all eight are answered.

**Done when:** the ADR lists these five as decided, not open, and the "still to decide" list has only three items left.

### Answer the last three design questions

- [x] **HIGH | UPDATED | NO DEADLINE** — answered 15 September 2026

All eight questions are now answered in ADR 0014, as D1-D8. The three answered this session:

1. **Does the decision gate still get asked when the data is untrusted?** Yes, always. Skipping it
   would mean nothing else could produce the "N blocked, reason: untrusted" count your own evidence
   rule requires. (D6)

2. **Which rejection reasons become permanent promises?** Only the ones a committed test checks by
   name. Anything untested stays free to rename — same rule that already killed `OperationalState`.
   Naming and testing the actual reasons is TODO item 3's job, not this ADR's. (D7)

3. **How long must the market look wrong before you stop trusting the book?** No number yet — none
   may be invented without evidence, and there isn't any. Every crossed period is blocked from
   trading regardless (via market shape), and its duration gets logged in the replay report so a
   real threshold can be set once the corpus shows how long crossings actually last. (D8)

**Reviewed and accepted 15 September 2026.** Reading D1-D8 side by side against the rest of the
document surfaced five stale spots written before D6-D8 existed — a status line still claiming "3
remain," a `DecisionGate` diagram still showing three Stage 5 inputs instead of two, a leftover "the
threshold remains open" line, an intro paragraph, and a timeline heading that said "D1-D5." All five
fixed. Nothing else contradicted.

**Done when:** the ADR lists all eight as decided, its status line reads accepted, all eight
questions are answered, deferred scope is stated, a hand-worked timeline exists, and every block has
a named reason. All true as of 15 September 2026.

## C. Repair the foundation

This was item 2a on the old list. This week gave it real evidence and exact locations. Do this before building more on top.

### Make the loader check the capture file is complete

- [x] **HIGH | UPDATED | NO DEADLINE** — done 16 September 2026, commit `aef6fc3`

`validateCapture()` compares the manifest's declared payload/frame-index size, hash, and frame/order/
trade/control counts against what was actually loaded, returning a named `ValidationError` on any
mismatch. `capture_coordinator.cpp` — the one production path from a capture directory to `replay()`
— now checks `loadSegment()`'s result before touching it, validates immediately after, and reads
`replay()`'s inputs, the cutoff, and the checkpoint comparison from the validated capture only. There
is no remaining path for an unvalidated capture to reach replay. A dedicated test proves a capture
that declares more payload bytes than it actually contains is rejected with
`CaptureCoordinatorError::capture_validation_failure` before replay runs.

`Replay::replay()` itself was deliberately left unchanged — it has 18+ existing direct call sites in
`test_bitstamp_replay.cpp`/`test_bitstamp_joined_capture.cpp` that test pure merge-ordering logic with
synthetic data and have nothing to do with capture files. The enforcement point is the coordinator,
the only real caller, not the primitive.

Not fully covered: `chain_valid` is read into the manifest (`declaredChainValid`) but `validateCapture()`
does not compare it against anything yet, and the recorder's `status` field isn't read into the
manifest struct at all. Left for a follow-up if a gap the byte/hash/count checks miss turns out to
need it.

Your recorder already writes down exactly what it produced. The C++ loader reads none of it. So a damaged, empty or half-written capture loads without complaint, and replay runs on it.

This week proved it: the capture file was empty, the loader returned "success, zero events", and the real problem only appeared much later as a confusing test failure.

**What The Recorder Already Records, And Nobody Checks**

- payload_bytes, payload_sha256 — size and fingerprint of the data file

- frames_bytes, frames_sha256 — same for the index file

- frames, order_events, trade_events, control_frames — expected counts

- chain_valid — whether the recorder saw any gaps

- status — whether the recording finished properly

Example: the 22 August capture declares 14,684,968 bytes and 29,490 frames.

**Steps**

1. Decide where the check lives. Three options, in the Questions section.

2. Read the size and count fields in manifest_reader.cpp. It currently reads only file paths.

3. Compare them against the real files before handing any events to replay.

4. Give every mismatch its own named error, so the message says what was wrong.

5. Make an empty capture a named failure instead of a silent success.

6. Add a test with a deliberately truncated capture.

Files: `src/capture/manifest_reader.cpp`, `src/capture/segment_loader.cpp`

The silent-success line is `segment_loader.cpp:101`.

**Done when:** a capture with a wrong size, wrong count or empty payload is rejected with a named reason, and replay cannot run on it.

### Fix the test that should skip but fails instead

- [ ] **MEDIUM | NEW | NO DEADLINE**

One test is meant to skip quietly when your private capture data is missing. It only checks that manifest.json exists. That small file survived; the big data file it points to did not. So the test ran anyway and reported a hard failure.

Check the data file is present and not empty, not just the manifest.

File: tests/unit/test_bitstamp_joined_capture.cpp, lines 349–354

**Done when:** deleting or emptying the capture data makes the test skip with a clear message, not fail.

### Guard the two unchecked sums

- [ ] **MEDIUM | UPDATED | NO DEADLINE**

Two places add numbers together without first checking the result will fit. Everywhere else in your code checks first. In C++, a whole number going past its limit is undefined behaviour — the compiler is allowed to assume it cannot happen, so the result is not simply a wrong number.

Real market sizes are far too small to trigger this. The point is consistency with your own standard.

- `src/feed/trade_reconciler.cpp:26` — adding up fills at the same timestamp
- `src/capture/capture_coordinator.cpp:81` and `:84` — adding up checkpoint sizes

Copy the pattern already in `src/book/price_level.cpp:11`.

**Done when:** both places check before adding, return a named error instead of overflowing, and a test proves it.

### Finish the order book's undo path

- [ ] **MEDIUM | UPDATED | NO DEADLINE**

When adding an order, the book first creates a new price level, then adds the order to it. If the second step runs out of memory and throws, the cleanup code never runs, because that cleanup only sits on the normal return path. An empty price level is left behind, and the error escapes as an exception instead of the error value the function promises.

An empty price level is a problem on its own: it can become the "best price" with nothing actually for sale behind it.

Same gap exists when an order changes price.

`src/book/order_book.cpp:57` (add) and `:125` (price change)

The throwing line is `src/book/price_level.cpp:15`.

**Decide First**

Is running out of memory something to recover from, or something to stop on? Today the code promises a recoverable error but does not always deliver one. Pick one and make it consistent.

**Done when:** a failed add or price change leaves the book exactly as it was, with no leftover empty level, and a test forces the failure.

### Run the Python tests in CI and pin the downloads

- [ ] **MEDIUM | UPDATED | NO DEADLINE**

CI runs only one Python test. Your recorder and capture-checker tests never run, so they can break without anyone noticing. Your C++ downloads also have no fingerprint, so you cannot prove you got the same file twice.

**Steps**

1. Add the Python test folder to CI.

2. Add a URL_HASH to each download in cmake/dependencies.cmake. Your own ADR 0001 already asks for this.

3. Write down which Python version and which websockets version you need.

4. Add a Release-mode test run, because some checks switch off outside debug builds.

- `.github/workflows/ci.yml:28` — currently the only Python line
- `cmake/dependencies.cmake:13` and `:19` — the two downloads

**Done when:** CI runs every Python test, both downloads have a fingerprint, and someone new can set up the project from the repository alone.

### Make the release-mode safety check real, or say it is not

- [ ] **LOW | NEW | NO DEADLINE**

validateStructure() checks the order book is internally consistent. It is built entirely from assertions, which switch off in release builds. So in release the function walks the whole book and checks nothing.

Either make the checks work in release too, or write plainly that this is a debug-only check.

src/book/order_book.cpp:196 ; called from src/feed/bitstamp/replay.cpp:172

**Done when:** the release build either really checks, or the documents stop implying it does.

## D. Build the trading engine

Unchanged from the old list except where marked. Do these in order — each one uses the one before. Do not start until ADR 0014 is accepted and section C is done.

### Build the money tracker, tests first

- [ ] **HIGH | NO DEADLINE**

Track cash, position, average cost, profit taken, profit on paper, and fees. Use whole numbers only — never decimals, which drift.

Write a buy, a partial sell, a final sell and fees out by hand first. Then build the class to match your arithmetic.

Also settle: currency scales, how cost is worked out, how you value open positions, rounding, and checked arithmetic in the middle of a calculation. Record every fill and fee with its own identity, so replaying the list rebuilds the account and the same fill cannot be counted twice.

**Done when:** tests reproduce every hand calculation exactly, tell taken profit apart from paper profit, cover long, flat and short, and refuse numbers that are too big without half-changing anything.

### Name the order types and rejection reasons

- [ ] **MEDIUM | NO DEADLINE**

Add only the small value types the accepted ADR needs: the order request, the operational state, the reason a decision was blocked, and the reason an order was rejected.

Keep three ideas separate: seeing the market, being allowed to decide, and being allowed to trade. Do not add an interface until something really needs it.

**Done when:** tests show that untrusted data, each untradeable market shape, and each scripted block produce their own distinct reason.

### Add the venue seam and a pretend exchange

- [ ] **MEDIUM | NO DEADLINE**

Start with submit, accept, reject, partial fill, full fill and cancel. A cancel that has been asked for is not a cancel that has happened — test an order filling while its cancel is still in flight.

The risk check looks at size, value and the position you would end up with. It must count orders already accepted but not yet filled, and check the worst case in both directions without assuming opposite orders fill at the same time.

The pretend exchange checks safety again before accepting, and records why. Build the no-delay version first. Add the delay queue only as the ADR says.

Rate limits, loss limits, the production kill switch and live recovery stay out. Do not add empty stand-ins that always say yes.

**Done when:** submit, accept, reject, fill and cancel can each be tested on their own, a rejection changes nothing, and a strategy cannot skip the risk check.

### Add the strategy seam and a do-nothing strategy

- [ ] **MEDIUM | NO DEADLINE**

Keep watching the market separate from deciding to trade. A strategy gets a read-only view of the book that it must not keep, and returns order requests. It never gets the real book or the ability to trade directly.

**Done when:** the do-nothing strategy sees every event, asks for nothing, builds against the real interface, and can be tested without the engine.

### Build the engine loop

- [ ] **MEDIUM | NO DEADLINE**

Wire together the feed, clock, strategy, decision gate, risk check, venue, order book, health tracker and money tracker. One thread. Follow the order the ADR sets before making anything faster.

**Done when:** a made-up timeline proves the book updates before the strategy sees it, watching continues while trading is blocked, decisions are gated, timing order is repeatable, and the money maths after a fill is exact. Test scenarios can be reused and can point at the first event where two runs differ.

### Pass the Stage 5 proof tests

- [ ] **MEDIUM | NO DEADLINE**

Section C must already pass. Then prove the whole path:

- The do-nothing strategy changes no counts, cash, position or profit over a real capture.

- One scripted strategy places a real order, and your hand-worked fill, fee and profit match end to end.

- At least one real risk rule rejects an order and changes nothing. A rule that always says yes does not count.

- Ten identical runs produce identical fingerprints for events, book, decisions, orders, cash, position and profit.

- Every blocked decision and rejected order is counted by name.

- Partial fills, cancel races, outstanding exposure and duplicate fills all have saved test cases. Replaying the fill and fee list rebuilds the account.

- Mid-run comparisons catch differences that identical totals would hide.

**Done when:** every one of these passes from a fresh copy of the project, without needing your private capture files.

### Ship the replay program and refresh the documents

- [ ] **LOW | NO DEADLINE**

Connect apps/replay_main.cpp to the build. It runs the saved capture through the real engine with the do-nothing strategy and prints a short report: where the data came from, counts, health, market shapes, decisions, orders, money and fingerprints.

Include a run record: input fingerprints, which commit and build, whether the project had uncommitted changes, instrument scales, settings, what was left out, policy versions and result fingerprints. Keep detailed step-by-step tracing optional.

Then update plan v4, the README, the handoff notes, and the placeholder comments so nothing still calls a finished module unfinished.

**Done when:** the build produces the replay program, its report comes out the same every time, and no current document calls a built module a placeholder.

## Questions for me

I did not guess at any of these. Answer them and I will fold them in.

1. Your message had an empty placeholder: "[paste new tasks, deadlines or changes]". Nothing was pasted, so I added nothing from it. Do you have tasks or changes to add?

2. Do you want any deadlines? There are no dates anywhere in the project, and you have not given me any, so every task says "No deadline". Tell me the dates and I will add them.

3. Where should the capture completeness check live? Three options. (a) Inside loadSegment — simplest, but then loading and checking are the same job. (b) A separate check that produces its own type, which replay is the only thing able to accept — this makes it impossible to replay unchecked data, but is the most work. (c) A step in the coordinator — cheap, but people can still call the loader directly and skip it. I would pick (b), because the mistake you just hit was exactly "something used data nobody checked".

4. Should the capture check get its own ADR? It is a contract decision, like ADR 0013 was. It could be ADR 0015, or it could just live inside the section C task. I lean towards its own ADR, because three separate programs have to agree on the same rules.

5. Move the whole project out of iCloud, or just the data/ folder? Moving everything is simplest and safest. Moving only data/ keeps your code backed up by iCloud, which is useful, but leaves the split to remember.

6. Is running out of memory recoverable, or fatal? Needed before the order book undo path can be finished properly. Right now the code half-promises recovery. Either answer is fine; it just has to be one of them.

7. Should the 54-page code manual be committed to the repository? It is at docs/TradingEngine-DeepDive.pdf and not yet in git. It is 2.3 MB.

## Done

Finished work, newest first. Kept short.

### This week

- [x] Wired capture validation into the coordinator: `capture_coordinator.cpp` now checks
  `loadSegment()`'s result before use, validates it against the manifest immediately after, and
  reads every downstream value (cutoff, replay inputs, checkpoint comparison) from the validated
  capture only, so an unvalidated capture has no remaining path to `replay()`. Commit `aef6fc3`,
  not yet pushed. 16 Sep

- [x] Recovered the capture data. It was not lost — iCloud had emptied the files. One command brought it back. 15 Sep

- [x] Confirmed the test suite passes 304 of 304 from a clean build. 15 Sep

- [x] Removed dead code in the order book — a duplicate line that could never run. 15 Sep

- [x] Corrected the fingerprint comments. The code says "FNV-1a" but uses a slightly different starting number. The comment now says so, and warns not to change it — every saved fingerprint depends on it. 15 Sep

- [x] Corrected the "order-independent" label on the applied-event fingerprint. It is order-sensitive, and that is deliberate. 15 Sep

- [x] Fixed the swapped labels in ADR 0008. "Cancels ahead of you" is the optimistic case, not the pessimistic one. 15 Sep

- [x] Tidied two mismatched function names in the replay code. 15 Sep

- [x] Recorded the fresh test result and the broken build folder in the handoff notes. 15 Sep

- [x] Wrote the 54-page code manual tracing one market message from raw bytes to finished book state. 15 Sep

- [x] Answered five of the eight open design questions — operational state, latency queue, first fill, time ordering and money ordering. 14–15 Sep

- [x] Fixed the health tracker so restarting from a corrupted state clears the old failure reason. Commit d00e244

- [x] Regenerated the old to-do PDF to match the eight-item list. Commit ad0f0a7

### Earlier foundation work

- [x] Merged the two decoders into one pass, keeping the old order-only decoder for the legacy recorder.

- [x] Turned on the CI guards for clock use and decimal numbers, with tests that deliberately break to prove they work.

- [x] Split the order book's internal checks from its market-shape reporting.

- [x] Separated "is the data trustworthy" from "what shape is the market", with both written down in CONTEXT.md.

- [x] Made the order book handle each event type properly, leaving state unchanged when an ordinary event is rejected, and checking itself after every successful change in debug builds.

- [x] Passed 303 tests after the trust and shape rework. 9 Sep

Working agreement: CLAUDE.md. Active checklist: TODO.md. Plain-language guide: docs/project-progress-guide.md. Refresh this list when a task closes or when reality disagrees with it.
