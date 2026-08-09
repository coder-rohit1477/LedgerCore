# LedgerCore

A modular C++17 double-entry accounting engine built around exact monetary arithmetic, immutable domain objects, explicit accounting invariants, a strictly layered architecture, deterministic reporting, and a test suite mapped directly to accounting properties.

## 1. Overview

LedgerCore is a from-scratch double-entry bookkeeping engine: a Chart of Accounts, balanced Journal Entries, a Ledger, a Posting Engine, Trial Balance snapshots (cumulative, as-of, and period-scoped), a small formula language for computed accounts, and Balance Sheet / Income Statement reporting.

It was designed as a reusable accounting *engine* rather than a single application: every accounting fact — an account, a journal entry, a posted balance, a report line — has exactly one authoritative representation and exactly one place where the rules governing it are enforced. Normal-balance sign conventions, balance checks, and overflow handling each exist in one location and are reused everywhere they're needed, rather than being re-derived per module.

The domain/ledger/posting/trialbalance/formula/computed/reporting core has no dependency on any presentation layer. There is currently no CLI, network interface, or persistence layer — the engine is a set of C++ libraries meant to be driven programmatically (a CLI is potential future work; see [Roadmap](#13-roadmap)).

This is a systems-design and testing-focused portfolio project. It is **not** production banking or accounting software, and makes no claim to regulatory compliance, multi-currency conversion, tax handling, or any other capability real accounting software would require.

## 2. Key Features

### Accounting Domain
- Chart of Accounts with hierarchical (tree) accounts
- Leaf vs. group account distinction — only leaf accounts can be posted to
- AccountType inheritance — a child account always inherits its parent's type
- Chart-wide unique AccountCode enforcement
- System-assigned, stable AccountId identity, distinct from AccountCode

### Money
- Exact integer minor-unit representation (`std::int64_t`, never `double`/`float`)
- Currency-safe arithmetic — operations across mismatched currencies throw
- Overflow detection on every arithmetic operation, checked before it executes
- Explicit `INT64_MIN` handling, including safe negation
- No floating-point accounting arithmetic anywhere in the codebase

### Journal Entries
- Balanced double-entry validation (`totalDebits == totalCredits`, enforced at construction)
- Currency consistency across all lines of one entry
- Immutable entries — no path to a partially-valid or later-mutated `JournalEntry`
- Debit/credit lines with strictly positive amounts

### Ledger & Posting
- Validate-then-commit posting: every fallible check runs before any Ledger mutation
- Atomic failure — a rejected posting leaves the Ledger completely unchanged
- Normal-balance-signed balances via one shared sign convention
- Append-only posted-entry history
- A single, explicit posting boundary (`posting::post`) — nothing else can mutate a `Ledger`

### Trial Balance
- Cumulative Trial Balance (`generate`)
- As-of Trial Balance (`generateAsOf`) — balances as of a cutoff instant
- Period Trial Balance (`generateForPeriod`) — activity within `[start, end)`
- Deterministic ordering by ascending AccountCode
- Leaf accounts only, including zero-activity accounts
- `totalDebits == totalCredits` verified on every generated Trial Balance

### Formula Engine
- Hand-written lexer and recursive-descent parser
- A small AST (literal, account reference, computed-account reference, unary/binary expression)
- Exact `Rational` arithmetic — never floating point
- Explicit dimensional rules for mixing `Money` and scalar values
- Overflow checking throughout
- Deterministic evaluation given the same AST and resolver

### Computed Accounts
- `@name`-style computed-account references, resolved against a registry
- Recursive dependency resolution, including diamond dependencies
- Cycle detection with a reported dependency path
- Deterministic evaluation
- Read-only with respect to the Ledger — evaluating a computed account never posts or mutates it

### Financial Reporting
- Balance Sheet (Assets / Liabilities / Equity)
- Income Statement (Revenue / Expenses / Net Income)
- Immutable snapshots derived from an already-generated Trial Balance
- Accounting-equation correctness verified by tests across randomized posting sequences

### Period Accounting
- Validated, half-open `Period` type: `[start, end)`
- As-of reporting (single cutoff) and period-activity reporting (start/end range)
- Business-date-based `JournalEntry` filtering — never posting order
- A whole `JournalEntry` is included or excluded as one unit; its lines are never split across a boundary

## 3. Architecture

```
                              ┌─────────────────┐
                              │    Reporting    │
                              │ (Balance Sheet,  │
                              │ Income Statement)│
                              └────────┬─────────┘
                                       │ uses
                                       ▼
┌────────────────┐   ┌─────────────────────────┐   ┌─────────────────┐
│     Posting     │   │      Trial Balance       │   │     Computed     │
│  (validate then  │   │ (cumulative / as-of /   │   │ (@name formula   │
│   commit)        │   │  period snapshots)      │   │  accounts)       │
└────────┬─────────┘   └────────────┬────────────┘   └────────┬─────────┘
         │ uses                     │ uses                     │ uses
         ▼                          ▼                          ▼
┌──────────────────────────────────────────┐          ┌─────────────────┐
│                   Ledger                   │          │     Formula      │
│  (posted-state truth: balances + history)  │          │ (lexer / parser /│
└──────────────────────┬─────────────────────┘          │  AST / evaluator)│
                        │ uses                            └────────┬─────────┘
                        ▼                                          │ uses
┌────────────────────────────────────────────────────────────────▼─────────┐
│                                     Domain                                  │
│   Account · ChartOfAccounts · Money · Currency · JournalEntry · Period ·   │
│         NormalBalance (isDebitNormal / signedEffect / debitCreditPresentation) │
└──────────────────────────────────────────────────────────────────────────┘
```

Every module above also depends directly on **Domain** for its core value types (`Money`, `AccountId`, `Currency`, ...) in addition to the arrows shown; Domain itself depends on nothing but the C++ standard library. `Computed` depends directly on both `Ledger` (to resolve real `#code` balances) and `Formula` (to parse and evaluate `@name` formulas).

This is dependency inversion applied literally: every arrow points toward `Domain`, never away from it. Nothing in `Domain` knows that `Ledger`, `Posting`, `TrialBalance`, `Formula`, `Computed`, or `Reporting` exist. Higher layers may depend on lower ones; a lower layer never depends on, includes, or links against a higher one. This graph is verified directly against the CMake target dependencies and the `#include` graph, not assumed from design intent.

## 4. Module Responsibilities

| Module | Responsibility | Dependencies |
|---|---|---|
| `domain` | Accounting primitives and invariants: `Account`, `ChartOfAccounts`, `Money`, `Currency`, `JournalEntry`, `Period`, and the single normal-balance sign convention | none (project-internal) |
| `ledger` | Posted-state truth: one signed balance per account, plus an append-only history of posted entries | `domain` |
| `posting` | The only component aware of both `JournalEntry` and `ChartOfAccounts`; validates and posts entries into a `Ledger` | `domain`, `ledger` |
| `trialbalance` | Cumulative / as-of / period-scoped snapshot projections of a `ChartOfAccounts` + `Ledger` pair | `domain`, `ledger` |
| `formula` | A small expression language (lexer, parser, AST, evaluator) over `Money` and exact `Rational` scalars | `domain` |
| `computed` | `@name` computed-account definitions, dependency resolution, and cycle detection, built on the formula engine | `domain`, `ledger`, `formula` |
| `reporting` | Balance Sheet and Income Statement, derived from an already-generated Trial Balance | `domain`, `trialbalance` |

## 5. Accounting Model

Each `AccountType` has a normal balance side — the side on which activity increases that account's balance:

| Account Type | Normal Balance | Increases On |
|---|---|---|
| Asset | Debit | Debit |
| Expense | Debit | Debit |
| Liability | Credit | Credit |
| Equity | Credit | Credit |
| Revenue | Credit | Credit |

In short: **Asset / Expense accounts are debit-positive; Liability / Equity / Revenue accounts are credit-positive.**

`Ledger` stores exactly one signed `Money` balance per `AccountId`, expressed under this normal-balance convention (`domain::signedEffect`) — not as a raw debit-minus-credit total. Because different accounts can have opposite normal-balance polarity, the raw signed balances stored in the `Ledger` do **not** sum to zero across all accounts; a balanced journal entry does not imply a zero-sum of stored `Ledger` balances.

What *is* guaranteed is the Trial Balance invariant: `TrialBalance` converts each account's signed `Ledger` balance back into a debit/credit presentation (`domain::debitCreditPresentation`, the exact inverse of `signedEffect`), and every `TrialBalance` generated from a correctly-posted `Ledger` satisfies:

```
totalDebits == totalCredits
```

This single conversion pair (`isDebitNormal` / `signedEffect` / `debitCreditPresentation`, all in `domain::NormalBalance`) is the one place the sign convention is defined; `posting`, `trialbalance`, and `reporting` all reuse it rather than re-deriving it.

## 6. Period Semantics

`Period` is a validated, half-open range: `[start, end)`.

```
April 2026:  [2026-04-01T00:00:00Z, 2026-05-01T00:00:00Z)
```

- `start` is included; `end` is excluded.
- Adjacent periods tile a timeline without overlap or gap — one period's `end` is the next period's `start`, and that shared instant belongs to the later period only.
- Filtering is based on a `JournalEntry`'s business `date()`, never its posting order — entries are not assumed to be posted in date order.
- A whole `JournalEntry` is included or excluded as a single unit; its lines are never split across a period boundary.

This produces two distinct report shapes:

- **`TrialBalance::generateAsOf(cutoff)`** — "as of" a single instant: includes every entry whose business date is strictly before `cutoff`.
- **`TrialBalance::generateForPeriod(period)`** — "for" a period: includes every entry whose business date falls inside `[period.start(), period.end())`.

`TrialBalance::generate(...)` (the original, cumulative form) is unchanged by this: it still reflects the Ledger's full running balance, with no date filtering at all.

## 7. Formula / Computed Account Example

A computed account is a name bound to a formula string, registered with `ComputedAccountRegistry::define`:

```cpp
registry.define(ComputedAccountName("GrossProfit"), "#4000 - #5000");
```

`#4000` and `#5000` are real Chart-of-Accounts references (resolved via a caller-supplied `AccountResolver`, typically backed by a `ChartOfAccounts` + `Ledger` pair). Once defined, `GrossProfit` can be referenced from another formula with `@GrossProfit`:

```cpp
registry.define(ComputedAccountName("DoubleGrossProfit"), "@GrossProfit * 2");
```

Evaluating `DoubleGrossProfit` recursively resolves `@GrossProfit`, which resolves `#4000` and `#5000` against the real ledger — with cycle detection across the whole dependency graph, and no mutation of the underlying `Ledger` at any point.

The two responsibilities are kept distinct:

```
Formula Engine   → parses and evaluates one expression string into a Money/Rational result
Computed Accounts → names formulas, resolves @name references against each other,
                     detects dependency cycles, and drives evaluation
```

The Formula Engine has no knowledge of `ComputedAccountRegistry`, `ChartOfAccounts`, or `Ledger` — it only knows `AccountResolver` and `ComputedAccountResolver`, two narrow abstractions supplied by the caller.

## 8. Testing

**360 tests**, all passing, organized as one GoogleTest executable per module plus a single smoke test.

| Module | Tests |
|---|---|
| domain (Account, ChartOfAccounts, Money, JournalEntry, NormalBalance, Period) | 120 |
| ledger | 6 |
| posting | 21 |
| formula (Lexer, Rational, Parser, Evaluator) | 95 |
| computed | 38 |
| trialbalance | 49 |
| reporting | 30 |
| smoke | 1 |

The suite mixes unit, integration, and property-style tests, targeted at the invariants the domain actually cares about rather than at raw line coverage:

- accounting invariants (balanced entries, unique account codes, normal-balance signs)
- Money/Rational overflow boundaries, including `INT64_MIN`
- posting atomicity (a failed post leaves the Ledger byte-for-byte unchanged)
- replay consistency (reconstructing balances from posted history matches the Ledger)
- computed-account cycle detection, including diamond dependencies that are *not* cycles
- period boundary behavior (`[start, end)` edges, adjacent-period tiling, backdated entries)
- reporting equations (Balance Sheet / Income Statement identities hold after randomized posting sequences)

Code coverage is not currently measured by this repository, so no coverage percentage is claimed.

## 9. Build & Test

Requires **CMake >= 3.20** and a **C++17** compiler. GoogleTest (v1.14.0) is fetched automatically via CMake `FetchContent` — no manual GoogleTest installation is needed.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## 10. Project Structure

```
LedgerCore/
├── cmake/
│   └── CompilerWarnings.cmake
├── src/
│   ├── domain/
│   ├── ledger/
│   ├── posting/
│   ├── trialbalance/
│   ├── formula/
│   ├── computed/
│   └── reporting/
├── tests/
│   ├── domain/
│   ├── ledger/
│   ├── posting/
│   ├── trialbalance/
│   ├── formula/
│   ├── computed/
│   ├── reporting/
│   └── smoke_test.cpp
├── CMakeLists.txt
└── README.md
```

Each `src/<module>/` directory contains its own `CMakeLists.txt`, `include/ledgercore/<module>/` (public headers), and `src/` (implementation); each `tests/<module>/` directory mirrors it with its own GoogleTest executable.

## 11. Design Principles

- **Domain-first architecture** — every dependency arrow points toward `domain`; nothing in `domain` knows any other module exists.
- **Explicit invariants, enforced structurally where possible** — e.g. the Chart of Accounts tree cannot contain a cycle because the API to construct one doesn't exist, not because a runtime check rejects it.
- **Immutable value/domain objects** — `Money`, `Currency`, `JournalEntry`, `Account`, `Period`, and generated snapshots (`TrialBalance`, `BalanceSheet`, `IncomeStatement`) have no setters and no path to a partially-valid state.
- **Validate-then-commit** — `posting::post` and `ChartOfAccounts` mutation both run every fallible check before touching any persistent state, so a rejected operation leaves nothing changed.
- **Exact monetary arithmetic** — `Money` and `Rational` are backed by `std::int64_t` with overflow checked before every operation; there is no floating point anywhere in an accounting calculation.
- **Single source of truth for normal-balance rules** — `domain::isDebitNormal` / `signedEffect` / `debitCreditPresentation` are defined once and reused by `posting`, `trialbalance`, and `reporting`.
- **Deterministic output** — the same inputs always produce the same `TrialBalance`, `BalanceSheet`, `IncomeStatement`, or formula evaluation result.
- **No premature persistence or caching** — snapshots are regenerated on demand from the `Ledger`; there is no cache to keep coherent.
- **Tests mapped to accounting invariants** — test names and property tests target specific accounting properties (balance, atomicity, replay consistency, cycle-freedom), not just code paths.

## 12. Current Status

The engine currently implements, in full: Chart of Accounts, Account hierarchy with AccountType inheritance, Money, Currency safety, exact integer-based monetary arithmetic, Journal Entries, Ledger, Posting Engine, cumulative/as-of/period-aware Trial Balance, the Formula Engine, Computed Accounts, Balance Sheet, and Income Statement.

- 360 tests, all passing
- Clean build, zero project compiler warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` and related flags, applied to every target)
- Production dependency graph verified directly against CMake target links and `#include` usage — no undocumented dependency exists

This is not a claim of production readiness — see [Overview](#1-overview).

## 13. Roadmap

Reasonable, currently-unimplemented future work:

- A CLI or other interactive interface driving the existing engine
- Recursion-depth hardening in the formula parser and computed-account dependency resolution, before either would ever accept untrusted input
- Richer fiscal-period abstractions (e.g. named fiscal calendars) built on top of the existing `Period` primitive
- Persistence (none exists today — all state is in-memory for the process lifetime)
- Additional reporting capabilities (e.g. comparative periods, cash flow statement)
- Performance work on full-history replay in `generateAsOf`/`generateForPeriod`, if a future use case demonstrates it's actually needed

None of the above is implemented today.

## 14. License

This repository does not currently include a `LICENSE` file. No license is claimed or implied here; treat the source as all-rights-reserved until a license file is added.
