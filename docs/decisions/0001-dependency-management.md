# ADR 0001: Dependency management

- **Status:** accepted
- **Date:** 2026-08-08
- **Slice:** 0

## Context

The project pulls in simdjson, GoogleTest, Boost.Beast, cppzmq and pybind11. They have to come from somewhere, on macOS now and Linux later on the VPS.

## Options considered

1. **FetchContent** — no extra tooling, slow clean builds, pins live in CMake.
2. **vcpkg** — manifest mode, good Boost support, another tool to install on the VPS.
3. **Homebrew / system packages** — fastest to start, worst reproducibility, breaks on the VPS.

## Decision

Use CMake `FetchContent` with dependencies pinned to specific release archives. The
dependency declarations and versions live in the repository so macOS development,
Linux CI and the future Linux VPS use the same dependency versions.

Add `URL_HASH` verification to release archives as dependencies are introduced or
updated. Version changes are deliberate repository changes and must pass CI before
being accepted.

## Consequences

- A contributor needs CMake and a C++ toolchain, but does not need to install or learn
  a separate package manager.
- Imported CMake targets propagate their include paths, compile requirements and link
  requirements to consumers.
- The first clean configuration requires network access and may be slower because it
  downloads and builds dependencies.
- Dependency updates are manual. Pins must be reviewed periodically for compatibility
  and security fixes.
- The project does not depend on machine-global Homebrew or Linux package versions.

## How you would defend this in an interview

This is a small cross-platform project, so reproducibility is more valuable than
minimizing clean-configuration time. FetchContent keeps reviewed dependency pins next
to the build configuration and gives the macOS workstation, Linux CI and deployment
machine the same versions without requiring another package-manager installation.
