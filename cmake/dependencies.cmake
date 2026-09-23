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
    URL_HASH SHA256=a4b6e7cd83176e0ccb107ce38521da40a8df41c2d3c90566f2a0af05b0cd05c4
    SYSTEM)
FetchContent_MakeAvailable(simdjson)

# --- Slice 0: tests -----------------------------------------------------------
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
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

# Pin versions by tag and hash, never by branch.
