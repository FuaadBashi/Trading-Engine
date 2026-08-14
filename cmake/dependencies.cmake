# ADR 0001: FetchContent, pinned by release tarball URL. Same approach as AudioVisualiser.
# Rationale: nothing extra to install on the macOS laptop or the Linux VPS, and the pin
# lives in this file rather than in a package manager's global state.
#
# SYSTEM on FetchContent_Declare (CMake 3.25+) marks dependency headers as system headers
# so their warnings do not trip our -Werror. Without it, -Wconversion inside simdjson
# would fail your build for someone else's code.

include(FetchContent)
find_package(Threads REQUIRED)

# --- Slice 1: JSON decode -----------------------------------------------------
FetchContent_Declare(simdjson
    URL https://github.com/simdjson/simdjson/archive/refs/tags/v3.9.1.tar.gz
    SYSTEM)
FetchContent_MakeAvailable(simdjson)

# --- Slice 0: tests -----------------------------------------------------------
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    SYSTEM)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- Slice 3: microbenchmarks -------------------------------------------------
# FetchContent_Declare(benchmark
#     URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
#     SYSTEM)
# set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(benchmark)

# --- Slice 5: websocket + telemetry transport ---------------------------------
# Boost.Beast and cppzmq. Both are heavier; decide in slice 5 whether FetchContent is
# still the right call for Boost or whether the system package wins there.

# TODO(fuaad): uncomment googletest now, simdjson at the start of slice 1.
# Pin versions by tag, never by branch. Check for newer releases when you uncomment.
