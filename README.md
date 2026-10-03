# LedgerCore

[![CI](https://github.com/coder-rohit1477/LedgerCore/actions/workflows/ci.yml/badge.svg)](https://github.com/coder-rohit1477/LedgerCore/actions/workflows/ci.yml)

A modular C++17 double-entry accounting engine built around exact monetary arithmetic, immutable domain objects, explicit accounting invariants, a strictly layered architecture, deterministic reporting, and a test suite mapped directly to accounting properties.

## 1. Overview

LedgerCore is a from-scratch double-entry bookkeeping engine: a Chart of Accounts, balanced Journal Entries, a Ledger, a Posting Engine, Trial Balance snapshots (cumulative, as-of, and period-scoped), a small formula language for computed accounts, and Balance Sheet / Income Statement reporting — plus a text snapshot format for saving and reloading a session, and a command-line interface (`ledgercore`) that drives all of it.

It was designed as a reusable accounting *engine* rather than a single application: every accounting fact — an account, a journal entry, a posted balance, a report line — has exactly one authoritative representation and exactly one place where the rules governing it are enforced. Normal-balance sign conventions, balance checks, and overflow handling each exist in one location and are reused everywhere they're needed, rather than being re-derived per module.

The domain/ledger/posting/trialbalance/formula/computed/reporting core has no dependency on any presentation or storage layer. Two thin layers sit on top of it: `persistence` (save/load of a whole session to a versioned text snapshot) and `cli` (an interactive REPL and a `--script` batch mode). There is no network interface, server, or database.

This is a systems-design and testing-focused portfolio project. It is **not** production banking or accounting software, and makes no claim to regulatory compliance, multi-currency conversion, tax handling, or any other capability real accounting software would require.

## 2. Key Features

### Accounting Domain
- Chart of Accounts with hierarchical (tree) accounts
- Leaf vs. group account distinction — only leaf accounts can be posted to
- AccountType inheritance — a child account always inherits its parent's type
- Chart-wide unique AccountCode enforcement
- System-assigned, stable AccountId identity, distinct from AccountCode
- An account with posting history can never become a group account: child accounts are added only through the ledger-aware `posting::addChildAccount` (the raw `ChartOfAccounts` operation is private)

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
- Documented reference grammar: `-` and `.` are valid account-code characters, so `#1000-1` names account `1000-1`; subtraction after a reference needs whitespace (`#1000 - 1`) — see [§7](#7-formula--computed-account-example)

### Computed Accounts
- `@name`-style computed-account references, resolved against a registry
- Recursive dependency resolution, including diamond dependencies
- Cycle detection with a reported dependency path
- Deterministic evaluation
- Read-only with respect to the Ledger — evaluating a computed account never posts or mutates it

### Financial Reporting
- Balance Sheet (Assets / Liabilities / Equity); the identity that holds is `Assets == Liabilities + Equity + Net Income`, where Net Income is whatever has not yet been closed, and the CLI prints that unclosed net income and the total alongside the Balance Sheet
- Income Statement (Revenue / Expenses / Net Income), reporting operating activity — closing entries are excluded, so closing a period never erases its income statement
- Immutable snapshots derived from an already-generated Trial Balance
- Accounting-equation correctness verified by tests across randomized posting sequences

### Closing Entries & Retained Earnings
- `closing::closeTemporaryAccounts` closes every Revenue and Expense balance into a designated Equity (retained-earnings) account through one balanced closing journal entry, posted through `posting::post` — see [§5](#closing-entries-and-retained-earnings)
- Invalid targets (non-Equity, group, unknown) and empty closes are rejected before the Ledger is touched
- Repeated closing is decided by the journal itself — no hidden "closed" flag

### Persistence
- Versioned, deterministic, line-oriented text snapshot (`LEDGERCORE-SNAPSHOT v1`, or `v2` when it contains a closing entry)
- AccountCode is the persisted account identity; AccountIds are regenerated on load
- Money written as exact integer minor units; strings quoted and escaped
- Journal entries are replayed on load through `JournalEntry::create` / `createClosing` and `posting::post` — never written into a Ledger directly
- Closing entries are written as `CLOSING` records (same layout as `ENTRY`) under a `v2` header; a snapshot without closing entries is still written byte-for-byte as `v1`, and both versions load
- All-or-nothing load: a malformed or invalid file throws and never yields a partial session
- Atomic save: temporary file, flush, close, then rename over the target
- Supported journal-entry dates: `[1900-01-01T00:00:00Z, 2200-01-01T00:00:00Z)`; out-of-range dates are rejected on save, on load, and by the CLI, never clamped

### Command-Line Interface
- `ledgercore` with no arguments starts an interactive REPL; `ledgercore --script <file>` runs commands from a file
- Commands: `account`, `post`, `trial-balance`, `balance-sheet`, `income-statement`, `formula eval`, `computed`, `close`, `save`, `load`, `exit`
- Dates are `YYYY-MM-DD` (UTC midnight), validated against real calendar days and the supported range
- `load` replaces the whole session atomically; a failed load leaves the current session untouched

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

This is dependency inversion applied literally: every arrow points toward `Domain`, never away from it. Higher layers may depend on lower ones; a lower layer never depends on, includes, or links against a higher one. The only upward *names* are two forward-declared `friend` grants used for access control: `Ledger` admits only `posting::post` to mutate it, and `ChartOfAccounts` admits only `posting::addChildAccount` to attach child accounts. Neither includes a higher-layer header or links a higher-layer library. This graph is verified directly against the CMake target dependencies and the `#include` graph, not assumed from design intent.

Two application-facing layers sit above the engine core:

```
cli (ledgercore executable) ──► persistence ──► posting, computed (──► ledger, formula, domain)
            │
            ├─────────────────► closing ──► posting, trialbalance (──► ledger, domain)
            │
            └─────────────────► reporting ──► trialbalance (──► ledger, domain)
```

`closing` is its own module because it needs both `posting` (to post the closing entry) and `trialbalance` (to read as-of balances), and `posting` must not depend on `trialbalance`.

`persistence` reconstructs a session only through the engine's public APIs (chart construction, `posting::addChildAccount`, `JournalEntry::create`, `posting::post`, `ComputedAccountRegistry::define`). `cli` owns one session (chart, ledger, computed registry) and translates text commands into those same APIs.

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
| `closing` | Closing entries: closes Revenue/Expense balances into a retained-earnings Equity account through one posted closing entry | `domain`, `ledger`, `posting`, `trialbalance` |
| `persistence` | Versioned text snapshot save/load of a whole session (chart, journal history, computed definitions), replaying history through `posting` | `domain`, `ledger`, `posting`, `computed` |
| `cli` | The `ledgercore` executable: REPL and `--script` modes, input parsing, report formatting, session ownership | `persistence`, `reporting` (and the rest transitively) |

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

This single conversion pair (`isDebitNormal` / `signedEffect` / `debitCreditPresentation`, all in `domain::NormalBalance`) is the one place the sign convention is defined; `posting`, `trialbalance`, `reporting`, and `closing` all reuse it rather than re-deriving it.

### Closing Entries and Retained Earnings

`closing::closeTemporaryAccounts(chart, ledger, retainedEarnings, cutoff)` closes all Revenue and Expense activity dated **strictly before `cutoff`** — exactly the balances `TrialBalance::generateAsOf(cutoff)` shows — into `retainedEarnings`, which must be a leaf Equity account. It posts one balanced `JournalEntryKind::Closing` entry through `posting::post`:

- each Revenue/Expense account with a non-zero balance gets one line on the opposite column of its trial-balance presentation (a credit balance is debited, a debit balance is credited), bringing it to zero;
- retained earnings gets the difference: a **credit** for net income, a **debit** for net loss (no line if revenue and expenses offset exactly).

```
Sales 700 Cr, Rent 250 Dr  ──close──►  Dr Sales 700 · Cr Rent 250 · Cr Retained Earnings 450
```

The closing entry is dated one clock tick before `cutoff`, so afterwards `generateAsOf(cutoff)` is a post-closing trial balance (temporary accounts zero, retained earnings holding the result), while anything dated at or after `cutoff` belongs to the next period.

- **Reports.** Trial balances and balance sheets include closing entries (post-closing view). Income statements exclude them (`trialbalance::ClosingEntries::Exclude`), so a closed year's income statement still reports its revenue, expenses, and net income.
- **Repeated closing.** A second close with the same cutoff finds every temporary balance already zero and throws `NothingToCloseException`; nothing is posted. If backdated activity is posted into an already-closed range, closing that cutoff again closes exactly the residual. There is no separate "period closed" state, and nothing prevents posting into a closed range.
- **Guard rail.** `posting::post` rejects a closing-kind entry that touches anything other than Revenue, Expense, and Equity accounts, so the closing marker can never hide ordinary activity — for entries closed live or replayed from a snapshot.

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

`#code` resolves to the account's normal-balance-signed Ledger balance (so `#4000 - #5000` is revenue minus expenses), and only leaf accounts can be referenced.

Reference grammar: after `#` (account) or `@` (computed account), a reference extends over every following letter, digit, `.`, `_`, or `-`. Hyphens and dots are deliberately valid code characters (codes like `1000-01` or `1000.10` are common numbering styles), so subtraction directly after a reference must be separated from it:

| Formula | Meaning |
|---|---|
| `#1000-1` | account `1000-1` |
| `#1000 - 1` or `(#1000)-1` | account `1000` minus 1 |
| `#1000 - #2000` | account `1000` minus account `2000` |
| `#1000-#2000` | syntax error (reference `1000-` followed by `#2000`) |
| `#1000+#2000` | account `1000` plus account `2000` |

The two responsibilities are kept distinct:

```
Formula Engine   → parses and evaluates one expression string into a Money/Rational result
Computed Accounts → names formulas, resolves @name references against each other,
                     detects dependency cycles, and drives evaluation
```

The Formula Engine has no knowledge of `ComputedAccountRegistry`, `ChartOfAccounts`, or `Ledger` — it only knows `AccountResolver` and `ComputedAccountResolver`, two narrow abstractions supplied by the caller.

## 8. Testing

**593 tests**, all passing, organized as one GoogleTest executable per module (two for the CLI) plus a single smoke test.

| Module | Tests |
|---|---|
| domain (Account, ChartOfAccounts, Money, JournalEntry, NormalBalance, Period) | 124 |
| ledger | 6 |
| posting | 33 |
| formula (Lexer, Rational, Parser, Evaluator) | 112 |
| computed | 38 |
| trialbalance | 52 |
| reporting | 30 |
| closing | 35 |
| persistence | 62 |
| cli (input parsing, command parsing, session, process-level end-to-end) | 100 |
| smoke | 1 |

The suite mixes unit, integration, and property-style tests, targeted at the invariants the domain actually cares about rather than at raw line coverage:

- accounting invariants (balanced entries, unique account codes, normal-balance signs)
- Money/Rational overflow boundaries, including `INT64_MIN`
- posting atomicity (a failed post leaves the Ledger byte-for-byte unchanged)
- replay consistency (reconstructing balances from posted history matches the Ledger)
- computed-account cycle detection, including diamond dependencies that are *not* cycles
- period boundary behavior (`[start, end)` edges, adjacent-period tiling, backdated entries)
- reporting equations (Balance Sheet / Income Statement identities hold after randomized posting sequences)
- closing entries (sign correctness for every account type, net income/loss, contra balances, invalid targets, atomic failure, repeated and multi-year closing, report consistency before and after closing)
- persistence round trips (save → load → save is byte-identical), corruption and version handling, failed-load isolation, date-range boundaries
- CLI behavior through the real executable (exit codes, REPL vs. script error handling, save/load)
- compile-time API guarantees (e.g. `Account` is neither copyable nor movable; the raw child-attach operation is not publicly callable)

Code coverage is not currently measured by this repository, so no coverage percentage is claimed.

## 9. Build & Test

Requires **CMake >= 3.20** and a **C++17** compiler. GoogleTest (v1.14.0) is fetched automatically via CMake `FetchContent` — no manual GoogleTest installation is needed.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To run the full suite under AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang only; off by default, so normal builds are unaffected), use a separate build directory:

```sh
cmake -S . -B build-sanitize -DLEDGERCORE_SANITIZE=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

Any UB report aborts the offending test process, so it surfaces as a CTest failure.

Journal entry dates are supported from 1900-01-01 up to (not including) 2200-01-01 UTC — the range the snapshot format's nanosecond timestamps can represent with margin. The CLI and persistence reject dates outside it rather than clamping them.

### Continuous Integration

GitHub Actions ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs on every push to `main` and every pull request, on `ubuntu-24.04`. Any configure, build, or test failure, sanitizer report, or clang-tidy finding fails the run; compiler warnings are reported (count in the job summary, each as an annotation) but not made fatal.

| Job | Toolchain / configuration | Checks |
|---|---|---|
| Build and test | GCC, `Release` | configure, build, full CTest suite, warning count, source tree left clean |
| ASan + UBSan | GCC, `Debug`, `-DLEDGERCORE_SANITIZE=ON` | confirms both sanitizers are compiled in and linked, then the full CTest suite (with LeakSanitizer) |
| Static analysis | clang-tidy 18 with [`.clang-tidy`](.clang-tidy) | every `src/**/*.cpp` file and project headers; any finding is an error |

To reproduce locally, use the build and sanitizer commands above (add `-DCMAKE_BUILD_TYPE=Release` or `Debug` to match CI), and for static analysis:

```sh
cmake -S . -B build-tidy -DCMAKE_CXX_COMPILER=clang++
find src -name '*.cpp' | xargs clang-tidy -p build-tidy --quiet
```

### Running the CLI

The executable is built at `build/src/cli/ledgercore`. A new session uses USD; a loaded snapshot keeps the currency it was saved with.

```sh
build/src/cli/ledgercore                    # interactive REPL
build/src/cli/ledgercore --script session.txt
```

Example script:

```
account create-root --code 1000 --name Cash --type asset
account create-root --code 4000 --name Sales --type revenue
post --date 2026-04-15 --description "Sold widgets" --debit 1000:250.00 --credit 4000:250.00
trial-balance
balance-sheet --as-of 2026-05-01
income-statement --from 2026-04-01 --to 2026-05-01
computed define --name GrossProfit --formula "#4000"
save books.snapshot
```

Closing a year into retained earnings (account `3100`, an Equity account) — all Revenue/Expense activity before 2027-01-01:

```
close --retained-earnings 3100 --as-of 2027-01-01
```

In the REPL, a failing command prints `error: ...` and the session continues. In `--script` mode the first failing command stops the run with exit code `1` (command syntax, a malformed date or amount, a date outside the supported range, or a snapshot file problem) or `2` (a rule enforced by the engine, e.g. an unbalanced entry, an unknown account, or posting to a group account); `3` means an unexpected internal error. A script that completes exits `0`.

## 10. Project Structure

```
LedgerCore/
├── .github/workflows/
│   └── ci.yml
├── cmake/
│   ├── CompilerWarnings.cmake
│   └── Sanitizers.cmake
├── src/
│   ├── domain/
│   ├── ledger/
│   ├── posting/
│   ├── trialbalance/
│   ├── formula/
│   ├── computed/
│   ├── reporting/
│   ├── closing/
│   ├── persistence/
│   └── cli/
├── tests/
│   ├── domain/
│   ├── ledger/
│   ├── posting/
│   ├── trialbalance/
│   ├── formula/
│   ├── computed/
│   ├── reporting/
│   ├── closing/
│   ├── persistence/
│   ├── cli/
│   └── smoke_test.cpp
├── .clang-tidy
├── CMakeLists.txt
└── README.md
```

Each library `src/<module>/` directory contains its own `CMakeLists.txt`, `include/ledgercore/<module>/` (public headers), and `src/` (implementation); each `tests/<module>/` directory mirrors it with its own GoogleTest executable. `src/cli/` is an executable only, with no public headers.

## 11. Design Principles

- **Domain-first architecture** — every dependency arrow points toward `domain`; nothing in `domain` knows any other module exists.
- **Explicit invariants, enforced structurally where possible** — e.g. the Chart of Accounts tree cannot contain a cycle because the API to construct one doesn't exist, not because a runtime check rejects it.
- **Immutable value/domain objects** — `Money`, `Currency`, `JournalEntry`, `Account`, `Period`, and generated snapshots (`TrialBalance`, `BalanceSheet`, `IncomeStatement`) have no setters and no path to a partially-valid state.
- **Validate-then-commit** — `posting::post` and `ChartOfAccounts` mutation both run every fallible check before touching any persistent state, so a rejected operation leaves nothing changed.
- **Exact monetary arithmetic** — `Money` and `Rational` are backed by `std::int64_t` with overflow checked before every operation; there is no floating point anywhere in an accounting calculation.
- **Single source of truth for normal-balance rules** — `domain::isDebitNormal` / `signedEffect` / `debitCreditPresentation` are defined once and reused by `posting`, `trialbalance`, and `reporting`.
- **Deterministic output** — the same inputs always produce the same `TrialBalance`, `BalanceSheet`, `IncomeStatement`, or formula evaluation result.
- **No report caching** — Trial Balance and report snapshots are regenerated on demand from the `Ledger`; there is no cache to keep coherent. Persistence stores only the chart, the journal history, and computed definitions, and rebuilds balances by replaying history.
- **Tests mapped to accounting invariants** — test names and property tests target specific accounting properties (balance, atomicity, replay consistency, cycle-freedom), not just code paths.

## 12. Current Status

Implemented: Chart of Accounts, Account hierarchy with AccountType inheritance, Money, Currency safety, exact integer-based monetary arithmetic, Journal Entries, Ledger, Posting Engine, cumulative/as-of/period-aware Trial Balance, the Formula Engine, Computed Accounts, Balance Sheet, Income Statement, closing entries into retained earnings, snapshot persistence, and the `ledgercore` CLI.

- 593 tests, all passing, in both the normal build and the AddressSanitizer/UndefinedBehaviorSanitizer build
- Clean build, zero project compiler warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` and related flags, applied to every project target)
- Production dependency graph verified directly against CMake target links and `#include` usage — no undocumented dependency exists

This is not a claim of production readiness — see [Overview](#1-overview).

## 13. Roadmap

Reasonable, currently-unimplemented future work:

- An explicit "period locked" state that rejects new postings into a closed range (today a backdated posting is allowed and simply leaves a residual for the next close)
- Recursion-depth hardening in the formula parser and computed-account dependency resolution, before either would ever accept untrusted input
- Richer fiscal-period abstractions (e.g. named fiscal calendars) built on top of the existing `Period` primitive
- Additional reporting capabilities (e.g. comparative periods, cash flow statement)
- Performance work on full-history replay in `generateAsOf`/`generateForPeriod`, if a future use case demonstrates it's actually needed

None of the above is implemented today.

## 14. License

This repository does not currently include a `LICENSE` file. No license is claimed or implied here; treat the source as all-rights-reserved until a license file is added.
