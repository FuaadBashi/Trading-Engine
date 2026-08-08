# ADR 0001: Dependency management

- **Status:** proposed
- **Date:** TODO
- **Slice:** 0

## Context

The project pulls in simdjson, GoogleTest, Boost.Beast, cppzmq and pybind11. They have to come from somewhere, on macOS now and Linux later on the VPS.

## Options considered

1. **FetchContent** — no extra tooling, slow clean builds, pins live in CMake.
2. **vcpkg** — manifest mode, good Boost support, another tool to install on the VPS.
3. **Homebrew / system packages** — fastest to start, worst reproducibility, breaks on the VPS.

## Decision

TODO

## Consequences

TODO

## How you would defend this in an interview

TODO
