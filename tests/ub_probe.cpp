// Deliberate undefined behaviour. Built ONLY with -DTE_BUILD_UB_PROBE=ON, never part of the
// ordinary test suite, and never linked into te_core or any app.
//
// This exists to test the sanitizer configuration rather than the engine. UBSan recovers from
// findings by default -- it prints the diagnostic and lets the process exit 0 -- so "UBSan is
// enabled" does not imply "UB fails the build". Before cmake/sanitizers.cmake gained
// -fno-sanitize-recover=undefined, this probe printed its error and still exited 0.
//
// Expected behaviour under a UBSan build: terminate with a NONZERO status before reaching the
// printf. The exact status is platform-specific (SIGABRT commonly surfaces as 134 on Linux
// shells), so callers must assert "not zero", never a specific number.
//
// If this ever exits 0 again, sanitizer findings have stopped failing the build and every other
// sanitized test has quietly become advisory.

#include <cstdint>
#include <cstdio>

int main(int argc, char**) {
    // argc is at least 1 and is not known at compile time, so the overflow happens at runtime
    // and cannot be constant-folded into a compile-time diagnostic. Signed overflow is UB, which
    // is what UBSan checks; an unsigned equivalent would wrap legally and prove nothing.
    std::int32_t value = 2147483647;
    value += argc;

    std::printf("UB PROBE DID NOT TERMINATE: sanitizer findings are not failing. value=%d\n",
                value);
    return 0;
}
