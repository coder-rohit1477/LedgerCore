# LedgerCore

A command-line double-entry accounting engine, built in modern C++17.

## Status

Phase 0 — project skeleton only. No domain logic yet.

## Architecture

Layered, dependency pointing inward:

```
CLI
Ledger   Formula
   Domain
```

- **Domain** depends only on the C++17 standard library.
- **Ledger** and **Formula** depend on Domain, never on each other.
- **CLI** depends on Ledger and Formula.

## Build

Requires CMake >= 3.20 and a C++17 compiler.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```
