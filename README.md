# LedgerCore

[![CI](https://github.com/coder-rohit1477/LedgerCore/actions/workflows/ci.yml/badge.svg)](https://github.com/coder-rohit1477/LedgerCore/actions/workflows/ci.yml)

A modular C++17 double-entry accounting engine built around exact monetary arithmetic, immutable domain objects, explicit accounting invariants, a strictly layered architecture, deterministic reporting, and a test suite mapped directly to accounting properties.

## 1. Overview

LedgerCore is a from-scratch double-entry bookkeeping engine: a Chart of Accounts, balanced Journal Entries, a Ledger, a Posting Engine, Trial Balance snapshots (cumulative, as-of, and period-scoped), a small formula language for computed accounts, and Balance Sheet / Income Statement reporting — plus a text snapshot format for saving and reloading a session, and a command-line interface (`ledgercore`) that drives all of it.

It was designed as a reusable accounting *engine* rather than a single application: every accounting fact — an account, a journal entry, a posted balance, a report line — has exactly one authoritative representation and exactly one place where the rules governing it are enforced. Normal-balance sign conventions, balance checks, and overflow handling each exist in one location and are reused everywhere they're needed, rather than being re-derived per module.

The domain/ledger/posting/trialbalance/formula/computed/reporting core has no dependency on any presentation or storage layer. Two thin layers sit on top of it: `persistence` (save/load of a whole session to a versioned text snapshot) and `cli` (an interactive REPL and a `--script` batch mode). There is no network interface, server, or database.

This is a systems-design and testing-focused portfolio project. It is **not** production banking or accounting software, and makes no claim to regulatory compliance, multi-currency conversion, tax handling, or any other capability real accounting software would require.

**Status:** version 1.0.1 (git tag `v1.0.1`; 1.0.1 adds the MIT license to the 1.0.0 release and changes no code). Released under the [MIT License](LICENSE). The public engine APIs, the CLI command set, and the snapshot formats `v1`–`v3` are considered stable as of 1.0. To see the whole system in a few minutes, build it and run the [demo walkthrough](#11-demo-walkthrough).

## 2. Engineering Highlights

Each item below is backed by the test suite or CI in this repository unless marked as a measurement.

- **Exact money.** `Money` is `std::int64_t` minor units, never floating point; every arithmetic operation is overflow-checked before it executes, including `INT64_MIN` negation. Formulas use exact `Rational` arithmetic.
- **Double-entry invariants.** A `JournalEntry` cannot be constructed unbalanced; `posting::post` is the only way to change a `Ledger` and validates everything before mutating anything; every generated Trial Balance verifies `totalDebits == totalCredits`; the Balance Sheet identity is checked across randomized posting sequences.
- **Determinism.** Reports are ordered by account code, never by hash-map order; save → load → save is byte-identical (tested, and checked again by the demo); a closing entry's timestamp is persisted identically on every platform.
- **Exception safety.** Posting, closing, adding accounts, and defining computed accounts are all-or-nothing. A dedicated test binary replaces the global allocator and re-runs each operation with every one of its allocations failing in turn, checking that no observable state changed.
- **Persistence through the front door.** Loading a snapshot rebuilds the session by replaying every journal entry through `posting::post`, so a hand-edited file is held to exactly the rules live callers are; a failed load never yields a partial session.
- **Bounded adversarial input.** Explicit limits on chart depth, formula depth, computed-evaluation depth, and closing-entry count (see [Resource Limits](#resource-limits-and-complexity)) turn hostile snapshots and formulas into ordinary errors instead of unbounded recursion or quadratic hangs.
- **Verification.** 736 tests, including two that build and run an external project against the installed (and relocated) CMake package. CI on every push: GCC 13 `Release`; GCC `Debug` with AddressSanitizer + UndefinedBehaviorSanitizer + LeakSanitizer; clang-tidy 18 with any finding fatal. The same suite is also run locally on macOS with AppleClang, in `Release` and under ASan/UBSan. Project targets build with zero warnings under `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion`.

Measured performance (one machine — Apple M5, 16 GB, AppleClang 21, `Release` — using an ad-hoc timing harness that is not part of this repository; indicative only, not a guarantee):

| Workload | Time |
|---|---|
| Post 1,000,000 two-line entries | ~0.25 s |
| As-of Trial Balance over those 1M entries (full replay) | ~11–16 ms |
| Journal query by account over 1M entries | ~17 ms |
| Save the 1M-entry session (65 MB snapshot) / load it (full replay through posting) | ~0.2 s / ~0.62 s |
| Chart of 100,000 leaf accounts: Trial Balance / load | ~13 ms / ~26 ms |
| 100,000 entries with 1,000 closing entries: post / load | ~1.25 s / ~0.52 s |
| 5.3 MB snapshot of 40,000 alternating closing entries (adversarial) | rejected in < 0.05 s at the closing-entry cap |
| Account tree 100,000 levels deep (adversarial) | rejected at depth 1,001 |

## 3. Key Features

### Accounting Domain
- Chart of Accounts with hierarchical (tree) accounts
- Leaf vs. group account distinction — only leaf accounts can be posted to
- AccountType inheritance — a child account always inherits its parent's type
- Chart-wide unique AccountCode enforcement
- Bounded tree depth (`ChartOfAccounts::kMaxDepth`, 1000 levels); every whole-tree operation is iterative, so stack use never grows with depth
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
- Bounded nesting (`formula::kMaxFormulaDepth`, 128 levels), so adversarially deep input fails with a syntax error instead of exhausting the stack
- Documented reference grammar: `-` and `.` are valid account-code characters, so `#1000-1` names account `1000-1`; subtraction after a reference needs whitespace (`#1000 - 1`) — see [§8](#8-formula--computed-account-example)

### Computed Accounts
- `@name`-style computed-account references, resolved against a registry
- Recursive dependency resolution, including diamond dependencies — each computed account is evaluated at most once per evaluation (per-call memoization), so diamonds cost linear time
- Bounded total evaluation depth across nested references (`ComputedAccountRegistry::kMaxEvaluationDepth`)
- Cycle detection with a reported dependency path
- Deterministic evaluation
- Read-only with respect to the Ledger — evaluating a computed account never posts or mutates it

### Financial Reporting
- Balance Sheet (Assets / Liabilities / Equity); the identity that holds is `Assets == Liabilities + Equity + Net Income`, where Net Income is whatever has not yet been closed, and the CLI prints that unclosed net income and the total alongside the Balance Sheet
- Income Statement (Revenue / Expenses / Net Income), reporting operating activity — closing entries are excluded, so closing a period never erases its income statement
- Immutable snapshots derived from an already-generated Trial Balance
- Accounting-equation correctness verified by tests across randomized posting sequences

### Closing Entries & Retained Earnings
- `closing::closeTemporaryAccounts` closes every Revenue and Expense balance into a designated Equity (retained-earnings) account through one balanced closing journal entry, posted through `posting::post` — see [§6](#closing-entries-and-retained-earnings)
- Invalid targets (non-Equity, group, unknown) and empty closes are rejected before the Ledger is touched
- Repeated closing is decided by the journal itself — no hidden "closed" flag

### Accounting Periods & Period Locking
- Accounting periods are `[start, end)` business-date ranges owned by the `Ledger`, with a one-way `Open → Closed` lifecycle
- `posting::post` rejects any entry — standard or closing — whose business date falls inside a closed period, before touching the Ledger
- Overlapping or duplicate periods are rejected; adjacent periods and gaps are allowed
- Locking never changes a balance, a posted entry, or any report — see [§7](#accounting-periods-and-period-locking)

### Journal History Queries
- `journalquery::findJournalEntries(ledger, query)` returns posted journal entries matching an immutable `JournalQuery`: business-date range (`[start, end)`), account involvement (any debit or credit line), and kind (all / standard only / closing only), combined with AND
- Results keep the Ledger's posting order and are lifetime-safe copies; querying is read-only — see [§7](#journal-history-queries-1)

### Persistence
- Versioned, deterministic, line-oriented text snapshot: `LEDGERCORE-SNAPSHOT v1`, `v2` when it contains a closing entry, `v3` when it defines accounting periods — always the lowest version that can represent the session
- AccountCode is the persisted account identity; AccountIds are regenerated on load
- Money written as exact integer minor units; strings quoted and escaped
- Journal entries are replayed on load through `JournalEntry::create` / `createClosing` and `posting::post` — never written into a Ledger directly
- Closing entries are written as `CLOSING` records (same layout as `ENTRY`) under a `v2` header; a snapshot without closing entries is still written byte-for-byte as `v1`
- Accounting periods are written as `PERIOD <start-ns> <end-ns> OPEN|CLOSED` records under a `v3` header, after every journal entry; all three versions load
- All-or-nothing load: a malformed or invalid file throws and never yields a partial session
- Atomic save: temporary file, flush, close, then rename over the target
- Supported journal-entry dates: `[1900-01-01T00:00:00Z, 2200-01-01T00:00:00Z)`; out-of-range dates are rejected on save, on load, and by the CLI, never clamped

### Command-Line Interface
- `ledgercore` with no arguments starts an interactive REPL; `ledgercore --script <file>` runs commands from a file
- Commands: `account`, `post`, `trial-balance`, `balance-sheet`, `income-statement`, `formula eval`, `computed`, `close`, `period`, `journal`, `save`, `load`, `exit`
- Dates are `YYYY-MM-DD` (UTC midnight), validated against real calendar days and the supported range
- `load` replaces the whole session atomically; a failed load leaves the current session untouched

### Period Accounting
- Validated, half-open `Period` type: `[start, end)`
- As-of reporting (single cutoff) and period-activity reporting (start/end range)
- Business-date-based `JournalEntry` filtering — never posting order
- A whole `JournalEntry` is included or excluded as one unit; its lines are never split across a boundary

## 4. Architecture

Modules form strict layers; each one depends only on modules in lower rows (arrows may skip rows), and every module also uses `domain` directly:

```
 cli            the ledgercore executable: REPL / --script, parsing, formatting
  │
 persistence    versioned snapshot save/load ─► posting, computed
  │
 reporting · closing · computed · journalquery
  │   reporting    ─► trialbalance        (Balance Sheet, Income Statement)
  │   closing      ─► posting, trialbalance (closing entries into retained earnings)
  │   computed     ─► ledger, formula     (@name accounts; read-only)
  │   journalquery ─► ledger              (journal history queries)
  │
 posting · trialbalance · formula
  │   posting      ─► ledger              (the only way to mutate a Ledger)
  │   trialbalance ─► ledger              (cumulative / as-of / period projections)
  │   formula      ─► domain              (lexer, parser, AST, exact evaluator)
  │
 ledger         posted-state truth: balances, journal history, accounting periods
  │
 domain         Account, ChartOfAccounts, Money, Currency, JournalEntry, Period, NormalBalance
```

`domain` depends on nothing but the C++ standard library, and a lower layer never depends on, includes, or links against a higher one. The only upward *names* are two forward-declared `friend` grants used for access control: `Ledger` admits only `posting::post` to mutate it, and `ChartOfAccounts` admits only `posting::addChildAccount` to attach child accounts. Neither includes a higher-layer header or links a higher-layer library. This graph is verified against the CMake target links and the `#include` graph, not assumed from design intent.

Why the layers look this way:

- **`posting` is the single mutation choke point.** Every balance change — live, from `closing`, or replayed by `persistence` — goes through `posting::post`, which runs every check before changing anything. Ledger-level rules (known leaf accounts only, matching currency, period locks, closing-entry completeness) are enforced in one place and cannot be bypassed; balance itself is guaranteed earlier, since an unbalanced `JournalEntry` cannot be constructed.
- **`domain::NormalBalance` is the only sign authority.** `isDebitNormal` / `signedEffect` / `debitCreditPresentation` define debit/credit polarity once; `posting`, `trialbalance`, `reporting`, and `closing` all reuse them, and computed `#code` references read the balances they produced.
- **`persistence` reconstructs state through normal posting.** A snapshot stores the chart, journal history, accounting periods, and computed definitions — never balances. Loading replays history through the public APIs, so balances are always re-derived and a hand-edited file faces the same rules as a live caller.
- **Reports consume trial-balance state.** Balance Sheet and Income Statement are built from an already-generated `TrialBalance`, so every report agrees with the trial balance it came from; nothing is cached.
- **Computed accounts are read-only.** They resolve `#code` balances from the `Ledger` and never post; the formula engine sees only two narrow resolver interfaces.
- **Journal queries return copies.** Results stay valid after later postings and cannot be used to alter history.
- **Resource limits sit at the input boundaries** — chart construction, formula parsing, computed evaluation, and closing-entry posting — the places a snapshot or formula can drive unbounded recursion or work (see [Resource Limits](#resource-limits-and-complexity)).

`closing` is its own module because it needs both `posting` (to post the closing entry) and `trialbalance` (to read as-of balances), and `posting` must not depend on `trialbalance`.

`persistence` reconstructs a session only through the engine's public APIs (chart construction, `posting::addChildAccount`, `JournalEntry::create`, `posting::post`, `ComputedAccountRegistry::define`). `cli` owns one session (chart, ledger, computed registry) and translates text commands into those same APIs.

## 5. Module Responsibilities

| Module | Responsibility | Dependencies |
|---|---|---|
| `domain` | Accounting primitives and invariants: `Account`, `ChartOfAccounts`, `Money`, `Currency`, `JournalEntry`, `Period`, and the single normal-balance sign convention | none (project-internal) |
| `ledger` | Posted-state truth: one signed balance per account, an append-only history of posted entries, and the accounting periods that govern which dates accept new postings | `domain` |
| `posting` | The only component aware of both `JournalEntry` and `ChartOfAccounts`; validates and posts entries into a `Ledger` | `domain`, `ledger` |
| `trialbalance` | Cumulative / as-of / period-scoped snapshot projections of a `ChartOfAccounts` + `Ledger` pair | `domain`, `ledger` |
| `formula` | A small expression language (lexer, parser, AST, evaluator) over `Money` and exact `Rational` scalars | `domain` |
| `computed` | `@name` computed-account definitions, dependency resolution, and cycle detection, built on the formula engine | `domain`, `ledger`, `formula` |
| `reporting` | Balance Sheet and Income Statement, derived from an already-generated Trial Balance | `domain`, `trialbalance` |
| `closing` | Closing entries: closes Revenue/Expense balances into a retained-earnings Equity account through one posted closing entry | `domain`, `ledger`, `posting`, `trialbalance` |
| `journalquery` | Read-only, deterministic filtering of posted journal history (date range, account, entry kind) | `domain`, `ledger` |
| `persistence` | Versioned text snapshot save/load of a whole session (chart, journal history, computed definitions, accounting periods), replaying history through `posting` | `domain`, `ledger`, `posting`, `computed`, `formula` |
| `cli` | The `ledgercore` executable: REPL and `--script` modes, input parsing, report formatting, session ownership | `persistence`, `reporting` (and the rest transitively) |

## 6. Accounting Model

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

The closing entry is dated one microsecond before `cutoff` (`domain::kClosingCutoffOffset` — exactly representable on every supported platform, so the same close is persisted identically everywhere), so afterwards `generateAsOf(cutoff)` is a post-closing trial balance (temporary accounts zero, retained earnings holding the result), while anything dated at or after `cutoff` belongs to the next period.

- **Reports.** Trial balances and balance sheets include closing entries (post-closing view). Income statements exclude them (`trialbalance::ClosingEntries::Exclude`), so a closed year's income statement still reports its revenue, expenses, and net income.
- **Repeated closing.** A second close with the same cutoff finds every temporary balance already zero and throws `NothingToCloseException`; nothing is posted. If backdated activity is posted into an already-closed range, closing that cutoff again closes exactly the residual — unless the range has also been *locked* as a closed accounting period ([§7](#accounting-periods-and-period-locking)), in which case such backdated postings are rejected in the first place.
- **Guard rail.** Because income statements exclude closing entries, `posting::post` accepts a closing-kind entry only if it is exactly a complete close: Revenue/Expense/Equity accounts only, each account at most once, at most one Equity destination, and every Revenue/Expense balance brought to zero as of the entry's cutoff (its date + 1µs). Partial, reversed, split, or padded "closing" entries are rejected — whether posted live or replayed from a hand-edited snapshot — so the marker can never hide or reshape ordinary activity.

## 7. Period Semantics

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

### Accounting Periods and Period Locking

An accounting period is a `domain::Period` — the same `[start, end)` business-date range `generateForPeriod` uses — registered on a `Ledger` with a state:

```
ledger.defineAccountingPeriod(Period(2027-01-01, 2028-01-01));   // Open
ledger.closeAccountingPeriod(Period(2027-01-01, 2028-01-01));    // Open -> Closed
```

- **Identity and shape.** A period is identified by its exact `[start, end)` bounds. Periods may not overlap (or duplicate), so no date ever belongs to two periods; adjacent periods and gaps are allowed, and periods may be defined in any order (they are kept, listed, and persisted in start order).
- **Lifecycle.** `Open → Closed`, one way. Closing an undefined period or closing twice is rejected. **Reopening is not supported**: there is no API for it, and redefining a closed range is rejected as an overlap.
- **What a closed period prevents.** `posting::post` rejects (`ClosedPeriodPostingException`) every entry — standard or closing — whose business date lies inside a closed period, before any Ledger mutation; the message names the posting date and the period. An entry dated exactly at the period's `end` belongs to the next period and is accepted. Open periods and dates outside every period are unrestricted.
- **What it does not do.** A lock is metadata about future postings only. It never changes a balance, a posted entry, a trial balance, a balance sheet, or an income statement — locking is not a reporting filter.

**Closing entries and period locking are separate operations.** *Closing entries transfer temporary-account balances* into retained earnings; *closing a period prevents subsequent postings into that period*. Neither implies the other:

- A period can be locked without a closing entry — e.g. lock months as they finish and close Revenue/Expense only at year end. The temporary balances simply stay open until a later close.
- A closing entry for cutoff `C` is dated `C − 1µs`, inside the period ending at `C`, so it must be posted **before** that period is locked. The supported year-end workflow is therefore: `close` the year, *then* lock the year's last period. Closing into an already-locked period is rejected like any other posting into it.

Snapshots persist each period's bounds and **current** state (format `v3`) — final-state semantics, not an audit log: a snapshot does not record *when* a period was closed, so it cannot tell whether a historical entry was posted before or after the close (live, only "before" is possible). Loading replays the whole journal first and then restores the periods, wherever their records appear in the file, so entries dated inside a closed period load as legitimate history, and the restored lock rejects any *new* posting dated inside it.

### Journal History Queries

`journalquery` is a read-only projection of `ledger.postedEntries()` — no second store, index, or cache:

```cpp
const auto entries = journalquery::findJournalEntries(
    ledger, JournalQuery()
                .withDateRange(Period(start, end))        // business date in [start, end)
                .withAccount(cashId)                      // on any debit or credit line
                .withKind(EntryKindFilter::StandardOnly)); // or ClosingOnly / All (default)
```

- **Filters** combine with AND; an unset filter matches everything. Entry kind comes only from `JournalEntry::kind()`, never from descriptions or line shapes.
- **Historical, not lock-aware.** A date-range query returns the same entries whether or not an accounting period covering those dates is open or closed — period locking restricts *postings*, never history.
- **Ordering.** Results are in the Ledger's posting order (ascending `PostingId`), the journal's only stable order. They are *not* re-sorted by business date: a backdated entry appears where it was posted, and entries sharing a date keep their posting order. Lines appear in their recorded order.
- **Results** are copies of the matching `PostedJournalEntry`s (posting id, business date, description, kind, currency, lines), so they stay valid after later postings. Cost is a linear scan: O(entries × lines per entry).
- **Persistence.** Queries operate on journal history only, so they return identical results before saving and after loading any v1/v2/v3 snapshot.

## 8. Formula / Computed Account Example

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

### Resource Limits and Complexity

The engine bounds every recursion and every super-linear cost that input can drive, so a malformed or adversarial snapshot, formula, or script is rejected with an ordinary LedgerCore error instead of overflowing the stack or hanging:

| Limit | Value | Bounds | On violation |
|---|---|---|---|
| `ChartOfAccounts::kMaxDepth` | 1000 levels | account-tree depth (checked when a child is attached, live or on load) | `ChartDepthExceededException` |
| `formula::kMaxFormulaDepth` | 128 levels | a formula's syntax-tree depth and parenthesis/unary nesting | `FormulaSyntaxException` |
| `ComputedAccountRegistry::kMaxEvaluationDepth` | 256 levels | the summed formula depth along a chain of nested `@name` references | `ComputedAccountDepthExceededException` |
| `posting::kMaxClosingEntries` | 1000 closing entries | closing entries one Ledger may hold (each one's validation replays history) | `ClosingEntryLimitExceededException` |

Tree traversals (trial balance, saving, account listings) and chart teardown are iterative. The formula limits were sized by measuring worst-case stack use, including under AddressSanitizer.

Costs worth knowing: posting, `hasPostingHistory`, and cumulative trial balances are O(1)/O(accounts) per call; as-of/period trial balances, journal queries, and closing are linear scans of history. Validating a closing entry replays history (O(history)), so a ledger with many closing entries pays O(closings × history) when posting them and again on load — about 1.2 s / 0.5 s for 1,000 closings over 100,000 entries. The closing-entry cap keeps that bounded: a snapshot can force at most 1,000 history replays, so load time stays proportional to file size instead of growing quadratically. All limits are LedgerCore errors, so the CLI reports them with exit code `2`.

These limits bound recursion depth and time, **not total memory**: input size itself is not capped, so a sufficiently large snapshot or script can still exhaust available memory. Typically that surfaces as `std::bad_alloc` (CLI exit code `3`), and posting, closing, adding accounts, and defining computed accounts are all-or-nothing under allocation failure; on a system that overcommits memory, the operating system may terminate the process instead.

## 9. Testing

**736 tests**, all passing: 734 GoogleTest cases, organized as one executable per module (two for the CLI) plus a single smoke test, and 2 CMake package tests.

| Module | Tests |
|---|---|
| domain (Account, ChartOfAccounts, Money, JournalEntry, NormalBalance, Period, date formatting, checked arithmetic) | 138 |
| ledger | 18 |
| posting | 59 |
| formula (Lexer, Rational, Parser, Evaluator) | 118 |
| computed | 44 |
| trialbalance | 54 |
| reporting | 30 |
| closing | 40 |
| journalquery | 22 |
| persistence | 84 |
| exception safety (allocation-failure injection) | 5 |
| cli (input parsing, command parsing, session, process-level end-to-end, the demo script) | 121 |
| smoke | 1 |
| package (an external `find_package` consumer: relocated install tree, and build tree) | 2 |

The suite mixes unit, integration, and property-style tests, targeted at the invariants the domain actually cares about rather than at raw line coverage:

- accounting invariants (balanced entries, unique account codes, normal-balance signs)
- Money/Rational overflow boundaries, including `INT64_MIN`
- posting atomicity (a failed post leaves the Ledger byte-for-byte unchanged)
- replay consistency (reconstructing balances from posted history matches the Ledger)
- computed-account cycle detection, including diamond dependencies that are *not* cycles
- period boundary behavior (`[start, end)` edges, adjacent-period tiling, backdated entries)
- reporting equations (Balance Sheet / Income Statement identities hold after randomized posting sequences)
- accounting periods (overlap rules, one-way lifecycle, `[start, end)` lock boundaries, atomic rejection of backdated postings, report invariance under locking, v3 final-state persistence and hand-edited period records)
- exception safety: every mutating engine operation (posting, closing, adding accounts, defining computed accounts) re-run with each of its allocations failing in turn, checking that nothing observable changed
- adversarial input: deep charts, deep formulas and dependency chains at and beyond every limit, and seeded fuzz-smoke tests that corrupt valid snapshots and feed random text to the formula and CLI parsers (no crash, only LedgerCore errors — under ASan/UBSan in CI)
- journal history queries (each filter and their combination, `[start, end)` boundaries, posting-order determinism, same-date ordering, exact Money/currency, read-only behaviour, identical results across save/load for v1/v2/v3)
- closing entries (sign correctness for every account type, net income/loss, contra balances, invalid targets, atomic failure, repeated and multi-year closing, report consistency before and after closing)
- persistence round trips (save → load → save is byte-identical), corruption and version handling, failed-load isolation, date-range boundaries
- CLI behavior through the real executable (exit codes, REPL vs. script error handling, save/load)
- compile-time API guarantees (e.g. `Account` is neither copyable nor movable; the raw child-attach operation is not publicly callable)

Code coverage is not currently measured by this repository, so no coverage percentage is claimed.

## 10. Build & Test

Requires **CMake >= 3.20** and a **C++17** compiler. Tested platforms: Linux with GCC 13 (CI also runs clang-tidy 18) and macOS with AppleClang. Windows is not currently supported — the end-to-end tests use POSIX process and file APIs. Configuring with tests enabled downloads GoogleTest (v1.14.0) via CMake `FetchContent`, so the first configure needs network access; no manual GoogleTest installation is needed.

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

Build options:

| Option | Default | Effect |
|---|---|---|
| `LEDGERCORE_BUILD_TESTS` | `ON` when LedgerCore is the top-level project, `OFF` when included by another project | builds the test suite (and fetches GoogleTest) |
| `LEDGERCORE_SANITIZE` | `OFF` | builds everything with ASan + UBSan |
| `LEDGERCORE_INSTALL` | `ON` when LedgerCore is the top-level project, `OFF` when included by another project | installs the engine library as the `LedgerCore` CMake package |

### Using LedgerCore as a Library

The engine is one CMake target, **`LedgerCore::ledgercore`**: all ten module libraries, their public headers (`#include "ledgercore/<module>/<Header>.h"`), and the C++17 requirement. It has no external dependencies, and it contains no CLI code — the `ledgercore` executable is a separate target that library users never link. (The installable package is on `main` after the `v1.0.1` tag; it is not yet part of a tagged release.)

**Installed package.** `cmake --install` places the headers, the static libraries, and a relocatable CMake package under the prefix (plus the `ledgercore` executable in `bin/`):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix /opt/ledgercore
```

```
/opt/ledgercore/
├── bin/ledgercore
├── include/ledgercore/<module>/*.h
└── lib/
    ├── libledgercore_<module>.a                 (10 module libraries)
    └── cmake/LedgerCore/                        LedgerCoreConfig.cmake, LedgerCoreConfigVersion.cmake,
                                                 LedgerCoreTargets.cmake, LedgerCoreTargets-<config>.cmake
```

```cmake
find_package(LedgerCore 1.0 CONFIG REQUIRED)    # e.g. with -DCMAKE_PREFIX_PATH=/opt/ledgercore
target_link_libraries(my_app PRIVATE LedgerCore::ledgercore)
```

```cpp
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/posting/PostingEngine.h"
#include "ledgercore/trialbalance/TrialBalance.h"
```

The package version comes from `project()`, with same-major-version compatibility (a request for `1.0` accepts any 1.x; `2.0` is rejected). The installed tree contains no absolute paths, so it can be copied or moved; [`tests/package/`](tests/package) verifies this by installing, moving the tree, then building and running an external consumer against it. A LedgerCore build directory can also be used without installing, via `-DLedgerCore_DIR=<build-dir>`.

**In-tree.** `add_subdirectory()` or `FetchContent` provides the same `LedgerCore::ledgercore` target (and the individual `ledgercore_<module>` targets) without installing anything; tests, the GoogleTest download, and install rules are off by default in this mode:

```cmake
add_subdirectory(LedgerCore)
target_link_libraries(my_app PRIVATE LedgerCore::ledgercore)
```

The libraries are static. Building them into a shared library needs position-independent code (`-DCMAKE_POSITION_INDEPENDENT_CODE=ON` when building LedgerCore).

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

Closing a year into retained earnings (account `3100`, an Equity account) — all Revenue/Expense activity before 2027-01-01 — and then locking that year against further postings (`--end` is exclusive):

```
period create --start 2026-01-01 --end 2027-01-01
close --retained-earnings 3100 --as-of 2027-01-01
period close --start 2026-01-01 --end 2027-01-01
period list
```

Querying journal history (`--from`/`--to` together, `--to` exclusive; `--standard` and `--closing` are mutually exclusive; filters combine):

```
journal
journal --from 2026-01-01 --to 2027-01-01
journal --account 4000 --standard
journal --closing
```

```
Journal (USD): 1 entry
#3  2026-12-31T23:59:59.999999000Z  closing  "Closing entry"
  DEBIT   4000      Sales                           700.00 USD
  CREDIT  5000      Rent                            250.00 USD
  CREDIT  3100      RetainedEarnings                450.00 USD
```

In the REPL, a failing command prints `error: ...` and the session continues. In `--script` mode the first failing command stops the run with exit code `1` (command syntax, a malformed date or amount, a date outside the supported range, or a snapshot file problem) or `2` (a rule enforced by the engine, e.g. an unbalanced entry, an unknown account, or posting to a group account); `3` means an unexpected internal error. A script that completes exits `0`.

## 11. Demo Walkthrough

[`examples/demo.txt`](examples/demo.txt) is a 27-command script that exercises the whole system: a chart with a group account, a fiscal year, three postings, a trial balance and income statement, two computed accounts (one referencing the other), a journal query, a year-end close into retained earnings, a period lock, a post-closing balance sheet, and a save → load → save round trip. A CTest case runs it on every build, so it stays in sync with the CLI.

```sh
cmake -S . -B build && cmake --build build
cd build                       # the demo writes its two snapshots here (ignored by git)
src/cli/ledgercore --script ../examples/demo.txt
cmp demo.snapshot demo-reloaded.snapshot && echo "reloaded session saves identically"
```

What to look for in the output:

- **Income statement:** Sales 700.00, Rent 250.00, Net Income 450.00.
- **Computed accounts:** `EstimatedTax` = `@NetIncome * 0.3` = `135.00 USD`, evaluated with exact rational arithmetic.
- **Closing:** `posted closing entry #4 into 3100 as of 2027-01-01 (net income 450.00 USD)`; `journal --closing` shows it dated `2026-12-31T23:59:59.999999000Z` (one microsecond before the cutoff), with Sales and Rent brought to zero.
- **Balance sheet:** Assets 700.00 = Liabilities 250.00 + Retained Earnings 450.00, unclosed net income 0.00.
- **Reload:** the trial balance after `load` matches the one before `save`, and `cmp` finds the two snapshots byte-identical. The snapshot itself (`demo.snapshot`) is plain text and worth reading: chart, `ENTRY`/`CLOSING` records in exact minor units, the `PERIOD` lock, and the computed definitions.

To see the period lock reject a backdated posting while later dates are still accepted, continue interactively from the saved snapshot:

```sh
printf '%s\n' 'load demo.snapshot' \
  'post --date 2026-12-15 --description "Late sale" --debit 1010:10.00 --credit 4000:10.00' \
  'post --date 2027-01-05 --description "New year sale" --debit 1010:10.00 --credit 4000:10.00' \
  | src/cli/ledgercore
# output includes:
# error: Cannot post an entry dated 2026-12-15 into closed accounting period [2026-01-01, 2027-01-01)
# posted entry #5
```

## 12. Project Structure

```
LedgerCore/
├── .github/workflows/
│   └── ci.yml
├── cmake/
│   ├── CompilerWarnings.cmake
│   ├── LedgerCoreConfig.cmake.in
│   ├── LedgerCorePackage.cmake
│   └── Sanitizers.cmake
├── examples/
│   └── demo.txt
├── src/
│   ├── domain/
│   ├── ledger/
│   ├── posting/
│   ├── trialbalance/
│   ├── formula/
│   ├── computed/
│   ├── reporting/
│   ├── closing/
│   ├── journalquery/
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
│   ├── journalquery/
│   ├── persistence/
│   ├── exceptionsafety/
│   ├── cli/
│   ├── package/
│   └── smoke_test.cpp
├── .clang-tidy
├── CMakeLists.txt
├── LICENSE
└── README.md
```

Each library `src/<module>/` directory contains its own `CMakeLists.txt`, `include/ledgercore/<module>/` (public headers), and `src/` (implementation); each `tests/<module>/` directory mirrors it with its own GoogleTest executable. `src/cli/` is an executable only, with no public headers.

## 13. Design Principles

- **Domain-first architecture** — every dependency arrow points toward `domain`; nothing in `domain` knows any other module exists.
- **Explicit invariants, enforced structurally where possible** — e.g. the Chart of Accounts tree cannot contain a cycle because the API to construct one doesn't exist, not because a runtime check rejects it.
- **Immutable value/domain objects** — `Money`, `Currency`, `JournalEntry`, `Account`, `Period`, and generated snapshots (`TrialBalance`, `BalanceSheet`, `IncomeStatement`) have no setters and no path to a partially-valid state.
- **Validate-then-commit** — `posting::post` and `ChartOfAccounts` mutation both run every fallible check before touching any persistent state, so a rejected operation leaves nothing changed.
- **Exact monetary arithmetic** — `Money` and `Rational` are backed by `std::int64_t` with overflow checked before every operation; there is no floating point anywhere in an accounting calculation.
- **Single source of truth for normal-balance rules** — `domain::isDebitNormal` / `signedEffect` / `debitCreditPresentation` are defined once and reused by `posting`, `trialbalance`, `reporting`, and `closing`.
- **Deterministic output** — the same inputs always produce the same `TrialBalance`, `BalanceSheet`, `IncomeStatement`, or formula evaluation result.
- **No report caching** — Trial Balance and report snapshots are regenerated on demand from the `Ledger`; there is no cache to keep coherent. Persistence stores only the chart, the journal history, accounting periods, and computed definitions, and rebuilds balances by replaying history.
- **Tests mapped to accounting invariants** — test names and property tests target specific accounting properties (balance, atomicity, replay consistency, cycle-freedom), not just code paths.

## 14. Known Limitations

- **One currency per ledger.** Cross-currency arithmetic is rejected, and there is no currency conversion.
- **Final-state persistence, not an audit log.** Snapshots record each accounting period's current state, not when it was locked; posting timestamps (`postedAt`) are not persisted.
- **Accounting periods cannot be reopened** (`Open → Closed` is one-way).
- **At most 1,000 closing entries per ledger.** Closing validation replays history, and the cap bounds that cost; it is a limit, not an incremental algorithm.
- **Memory is not bounded.** The resource limits bound recursion and time, not input size (see [Resource Limits](#resource-limits-and-complexity)).
- **Static libraries only, no prebuilt binaries.** LedgerCore is built from source with CMake; there is no shared-library build option and no package-manager (vcpkg/Conan) recipe.
- **Linux and macOS only.** Windows is not supported; the end-to-end test harness uses POSIX APIs.
- **No `--version` flag.** The version is defined once, in the top-level `CMakeLists.txt` `project()` call, and stated in this README.
- **No coverage measurement**, so no coverage percentage is claimed.

## 15. Current Status

Version **1.0.1**, tagged `v1.0.1` (identical in behaviour to `v1.0.0`; adds the license). Implemented: Chart of Accounts, Account hierarchy with AccountType inheritance, Money, Currency safety, exact integer-based monetary arithmetic, Journal Entries, Ledger, Posting Engine, cumulative/as-of/period-aware Trial Balance, the Formula Engine, Computed Accounts, Balance Sheet, Income Statement, closing entries into retained earnings, accounting periods with period locking, journal history queries, snapshot persistence, and the `ledgercore` CLI.

- 736 tests, all passing, in the normal build and the AddressSanitizer/UndefinedBehaviorSanitizer build, on GCC (CI) and AppleClang
- Clean build, zero project compiler warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion` and related flags, applied to every project target)
- Production dependency graph verified directly against CMake target links and `#include` usage — no undocumented dependency exists

This is not a claim of production readiness — see [Overview](#1-overview).

## 16. Roadmap

Reasonable, currently-unimplemented future work:

- Reopening a closed accounting period (deliberately unsupported today: `Open → Closed` is one-way)
- Windows support (the engine is portable C++17; the end-to-end test harness is POSIX-only)
- A `ledgercore --version` flag reporting the CMake project version
- Incremental closing-entry validation, which would remove the need for the closing-entry cap
- Coverage-guided fuzzing (libFuzzer) of the snapshot, formula, and CLI parsers; today they are covered by seeded fuzz-smoke tests
- Richer fiscal-period abstractions (e.g. named fiscal calendars) built on top of the existing `Period` primitive
- Additional reporting capabilities (e.g. comparative periods, cash flow statement)
- Performance work on full-history replay in `generateAsOf`/`generateForPeriod`, if a future use case demonstrates it's actually needed

None of the above is implemented today.

## 17. License

LedgerCore is released under the MIT License. See [`LICENSE`](LICENSE) for the full text.
