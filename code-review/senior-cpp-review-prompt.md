# Senior C++ Review Prompt (Low-Latency Trading Systems)

**How to use:** copy this whole file into a chat, replace the two `[...]` placeholders at the
bottom (code to review, and relevant context — ADRs, tests, invariants, perf goals), and send it.
Works for a single method, a whole file, or a diff. Nothing in this file is project-specific
infrastructure — it's a prompt, safe to paste as-is anywhere.

---

Perform a read-only, senior-level C++ code review of the code or files I provide.

IMPORTANT:
- Do not edit, patch, format, commit, or otherwise modify any files.
- Do not implement changes in the repository.
- Show all proposed code only in this chat.
- First understand the surrounding code, tests, ADRs, invariants, ownership rules, and performance requirements.
- Do not recommend changes merely because they are shorter or more "clever."
- Preserve the existing behaviour unless you clearly identify a bug or contract problem.

Review the code as a senior C++ engineer working on a low-latency quantitative trading system.

Evaluate it in this order:

## 1. Correctness and safety
- Undefined behaviour
- Invalid iterators, references, pointers, or object lifetimes
- Integer overflow and unsafe conversions
- Partial state changes when an operation fails
- Exception safety and rollback behaviour
- Ownership mistakes
- Incorrect assumptions about market data
- Broken invariants
- Nondeterministic behaviour

## 2. Domain correctness
- Price, quantity, side, order ID, timestamp, and queue-position semantics
- Fixed-point arithmetic rather than floating point
- Add, modify, remove, snapshot, reconnect, and gap behaviour
- Whether the code distinguishes structural correctness from market usability
- Whether replay and live processing follow the same rules
- Whether failures leave the order book unchanged
- Whether a recommendation would damage deterministic replay

## 3. Interface and design quality
- Whether each class owns the correct responsibility
- Whether the public interface is smaller and clearer than the implementation
- Whether names describe domain meaning
- Whether callers can misuse the interface
- Whether invariants should be enforced by the type system
- Whether a helper function genuinely simplifies the code
- Whether duplicated code should be removed or deliberately retained for clarity

## 4. Readability and maintainability
- Simpler control flow
- Early returns
- Better iterator and variable names
- Removal of unnecessary lookups or repeated expressions
- Removal of stale or misleading comments
- Use of const, references, structured bindings, algorithms, or standard-library facilities
- Opportunities to express intent more directly

## 5. Performance
- Algorithmic complexity
- Repeated map or hash-table lookups
- Unnecessary allocations, copies, moves, or temporary objects
- Cache locality and data layout
- Branching in likely hot paths
- Hashing costs
- Iterator stability
- Locking or synchronization costs
- Parsing and serialization overhead
- Debug checks accidentally placed in production hot paths

Do not claim that something is faster without explaining why.

Classify every performance suggestion as one of:
- Proven from complexity
- Highly likely from data layout or allocation behaviour
- Plausible but requires benchmarking
- Cosmetic and unlikely to matter

For changes that require measurement, specify:
- What benchmark to write
- What input data to use
- What metric to collect
- What result would justify the change
- What correctness checks must remain enabled

## 6. Advanced modern C++ opportunities

Consider these only when they genuinely improve the code:
- Strong types
- Concepts or constrained templates
- constexpr or consteval
- std::span and std::string_view
- std::optional, std::variant, or an explicit Result type
- Transparent comparators and heterogeneous lookup
- try_emplace, insert_or_assign, contains, and node handles
- Ranges and algorithms
- Custom allocators, std::pmr, arenas, or object pools
- Flat containers or contiguous storage
- Branch prediction hints
- Compile-time contracts and static_assert
- Move-only ownership
- noexcept
- [[nodiscard]]
- Likely/unlikely attributes
- Prefetching, SIMD, and cache-line alignment

Reject any technique that adds complexity without a realistic benefit. Never recommend low-level
tricks that introduce undefined behaviour, weaken correctness, or obscure the domain rules.

## Output format

### A. Executive assessment

Give a short assessment of:
- What is already good
- The largest correctness risk
- The largest design weakness
- The most promising performance improvement
- Whether the code is currently suitable as a correctness-first reference implementation

### B. Findings ranked by severity

Use these categories:
- **Critical:** can corrupt state, produce wrong financial results, invoke undefined behaviour, or break replay determinism
- **Important:** creates fragile design, unnecessary cost, or likely future bugs
- **Improvement:** useful cleanup with limited risk
- **Leave alone:** code that may look simple or repetitive but is currently the better design

For every finding provide:
- Exact file and function
- The relevant current code
- The specific problem
- A concrete example showing when it matters
- A complete proposed replacement snippet
- Why the replacement is better
- Runtime complexity before and after
- Allocation/copy/lookup differences
- Risks and trade-offs
- Tests or benchmarks required before adoption

### C. Three alternative versions

Where useful, show:

1. **Teaching version** — most explicit, easy for a developing C++ programmer to understand
2. **Production reference version** — clear, robust, deterministic, and maintainable; the version
   you recommend for the current project
3. **Optimized hot-path version** — only when measurement could justify it; explain the additional
   complexity and conditions required

Do not present an optimized version if the code is not actually on a measured hot path.

### D. Interesting C++ techniques

Explain any valuable techniques separately and simply:
- What the language feature does
- Why it might help here
- A small unrelated example
- The version applied to my code
- When I should not use it
- Whether it improves correctness, readability, or performance

### E. Recommended order of adoption

Finish with:
1. Changes required for correctness
2. Changes that improve clarity
3. Changes worth benchmarking
4. Changes to postpone
5. Changes to reject

## Teaching requirement

Assume I am learning C++ and quantitative-development practices. Explain important terms in plain
English while retaining their proper technical names.

Do not merely provide improved code. Teach me how to recognize the underlying pattern myself.

For each recommendation, give me one question to answer before I adopt it. The question should
test whether I understand the reasoning rather than whether I memorized the syntax.

---

## Code to review

[PASTE CODE OR LIST FILE PATHS HERE]

## Relevant contracts, ADRs, tests, or performance goals

[PASTE CONTEXT HERE]
