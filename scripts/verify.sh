#!/usr/bin/env bash
# One command to build the engine and check the replay claims the README makes.
#
#   ./scripts/verify.sh
#   BUILD_DIR=/tmp/te-verify ./scripts/verify.sh
#
# Builds Release outside the source tree, runs the whole suite, then lists each replay claim
# with its result on this machine. The real-capture case needs gitignored data; without it the
# claim is reported as "not run", never counted as a pass.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=${BUILD_DIR:-$HOME/build/TradingEngineProject-verify}
jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
log=$(mktemp)
trap 'rm -f "$log"' EXIT

echo "==> Configuring Release build in $build"
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release >/dev/null
echo "==> Building"
cmake --build "$build" -j "$jobs"
echo "==> Running the test suite"
set +e
ctest --test-dir "$build" --output-on-failure -j "$jobs" | tee "$log"
suite_status=${PIPESTATUS[0]}
set -e

# ctest prints one line per case ending in Passed, ***Skipped or ***Failed.
result() {
    local line
    line=$(grep -E "Test +#[0-9]+: $1 " "$log" || true)
    case "$line" in
        "") echo "missing" ;;
        *Passed*) echo "pass" ;;
        *Skipped*) echo "not run" ;;
        *) echo "FAIL" ;;
    esac
}

claims_failed=0
report() { # test name, description, whether "not run" is acceptable
    local r
    r=$(result "$1")
    printf '  %-8s %s\n' "$r" "$2"
    # A missing golden case means it was renamed or deleted, which would silently drop the claim.
    if [ "$r" = "FAIL" ] || [ "$r" = "missing" ] || { [ "$r" = "not run" ] && [ "$3" = "required" ]; }; then
        claims_failed=1
    fi
}

echo
echo "Replay claims checked on this machine:"
report "BitstampJoinedCapture.GoldenFixtureReplaysToHandWrittenCheckpoint" \
    "committed golden capture replays to its hand-written checkpoint" required
report "CaptureCoordinator.GoldenCaptureMatchesIndependentCheckpoint" \
    "capture coordinator's book matches an independently written checkpoint" required
report "CaptureCoordinator.GoldenCaptureProducesIdenticalDigestsAcrossRepeatedRuns" \
    "repeated runs produce identical book digests (determinism)" required
report "BitstampJoinedCapture.RealCaptureReplaysToCheckpointWithNoResiduals" \
    "29k-event Bitstamp capture replays to the venue's checkpoint (needs gitignored data/raw/)" optional

if [ "$suite_status" -ne 0 ] || [ "$claims_failed" -ne 0 ]; then
    echo "Verification FAILED."
    exit 1
fi
echo "Verification passed."
