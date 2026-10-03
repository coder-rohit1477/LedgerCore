// Process-level tests: each spawns the actual built ledgercore_cli binary
// against a small script (see the "Input Modes" section of the Phase 11
// design) and asserts on its combined stdout+stderr and exit code. These
// verify CLI wiring and presentation only -- every accounting invariant
// exercised along the way (balance checks, account resolution, period
// boundaries, report equations) is already covered by the existing
// domain/ledger/posting/trialbalance/reporting test suites and is not
// re-verified here.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>

#ifndef LEDGERCORE_CLI_PATH
#error "LEDGERCORE_CLI_PATH must be defined by CMake"
#endif

namespace {

struct RunResult {
    int exitCode = 0;
    std::string output;
};

std::string uniqueTempPath(const std::string& label) {
    static int counter = 0;
    ++counter;
    return "/tmp/ledgercore_cli_e2e_" + label + "_" + std::to_string(::getpid()) + "_" + std::to_string(counter)
           + ".txt";
}

// Writes `script` to a temp file, runs the CLI binary against it via
// --script <path>, and captures combined stdout+stderr and the process
// exit code.
RunResult runScript(const std::string& script) {
    const std::string scriptPath = uniqueTempPath("script");
    {
        std::ofstream out(scriptPath);
        out << script;
    }

    const std::string command = std::string("\"") + LEDGERCORE_CLI_PATH + "\" --script \"" + scriptPath + "\" 2>&1";

    RunResult result;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe != nullptr) {
        char buffer[256];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result.output += buffer;
        }
        const int status = pclose(pipe);
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    } else {
        result.exitCode = -1;
    }

    std::remove(scriptPath.c_str());
    return result;
}

// Writes `input` to a temp file and runs the CLI binary in REPL mode with
// stdin redirected from it (shell '<' redirection -- popen only gives a
// one-directional pipe, so this is simpler than a bidirectional one).
// This exercises genuine REPL behavior, including that a failed command
// must not terminate the session, unlike --script mode.
RunResult runRepl(const std::string& input) {
    const std::string inputPath = uniqueTempPath("repl_input");
    {
        std::ofstream out(inputPath);
        out << input;
    }

    const std::string command = std::string("\"") + LEDGERCORE_CLI_PATH + "\" < \"" + inputPath + "\" 2>&1";

    RunResult result;
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe != nullptr) {
        char buffer[256];
        while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result.output += buffer;
        }
        const int status = pclose(pipe);
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    } else {
        result.exitCode = -1;
    }

    std::remove(inputPath.c_str());
    return result;
}

} // namespace

TEST(CliEndToEndTest, CreateAccountsPostBalancedEntryThenTrialBalanceSucceeds) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 4000 --name Sales --type revenue\n"
        "post --date 2026-04-15 --description \"Sold widgets\" --debit 1000:250.00 --credit 4000:250.00\n"
        "trial-balance\n");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("posted entry #1"), std::string::npos);
    EXPECT_NE(result.output.find("250.00 USD"), std::string::npos);
    EXPECT_NE(result.output.find("TOTAL"), std::string::npos);
}

TEST(CliEndToEndTest, UnbalancedPostExitsWithCodeTwo) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 4000 --name Sales --type revenue\n"
        "post --date 2026-01-01 --description bad --debit 1000:100.00 --credit 4000:99.00\n");

    EXPECT_EQ(result.exitCode, 2);
    EXPECT_NE(result.output.find("does not balance"), std::string::npos);
}

TEST(CliEndToEndTest, UnknownAccountExitsWithCodeTwo) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "post --date 2026-01-01 --description bad --debit 1000:100.00 --credit 9999:100.00\n");

    EXPECT_EQ(result.exitCode, 2);
    EXPECT_NE(result.output.find("No account found"), std::string::npos);
}

TEST(CliEndToEndTest, ComputedDefineThenEvalSucceeds) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 4000 --name Sales --type revenue\n"
        "account create-root --code 5000 --name COGS --type expense\n"
        "post --date 2026-01-01 --description sale --debit 1000:100.00 --credit 4000:100.00\n"
        "post --date 2026-01-01 --description cogs --debit 5000:40.00 --credit 1000:40.00\n"
        "computed define --name GrossProfit --formula \"#4000 - #5000\"\n"
        "computed eval GrossProfit\n");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("defined computed account GrossProfit"), std::string::npos);
    EXPECT_NE(result.output.find("60.00 USD"), std::string::npos);
}

TEST(CliEndToEndTest, PeriodBoundaryTrialBalanceExcludesEntryOnBoundary) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 4000 --name Sales --type revenue\n"
        "post --date 2026-04-01 --description inside --debit 1000:100.00 --credit 4000:100.00\n"
        "post --date 2026-05-01 --description on-boundary --debit 1000:200.00 --credit 4000:200.00\n"
        "trial-balance --from 2026-04-01 --to 2026-05-01\n");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("100.00 USD"), std::string::npos);
    // The second entry falls exactly on the exclusive end boundary and
    // must not appear in the totals.
    EXPECT_EQ(result.output.find("200.00 USD"), std::string::npos);
}

TEST(CliEndToEndTest, BalanceSheetAsOfExcludesLaterActivity) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 3000 --name OwnerEquity --type equity\n"
        "post --date 2026-01-01 --description seed --debit 1000:500.00 --credit 3000:500.00\n"
        "post --date 2026-06-01 --description later --debit 1000:100.00 --credit 3000:100.00\n"
        "balance-sheet --as-of 2026-06-01\n");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("Balance Sheet"), std::string::npos);
    EXPECT_NE(result.output.find("500.00 USD"), std::string::npos);
    EXPECT_EQ(result.output.find("100.00 USD"), std::string::npos);
}

TEST(CliEndToEndTest, IncomeStatementFromToReportsOnlyPeriodActivity) {
    const RunResult result = runScript(
        "account create-root --code 4000 --name Sales --type revenue\n"
        "account create-root --code 1000 --name Cash --type asset\n"
        "post --date 2026-03-01 --description before --debit 1000:900.00 --credit 4000:900.00\n"
        "post --date 2026-04-10 --description during --debit 1000:150.00 --credit 4000:150.00\n"
        "income-statement --from 2026-04-01 --to 2026-05-01\n");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("Income Statement"), std::string::npos);
    EXPECT_NE(result.output.find("150.00 USD"), std::string::npos);
    EXPECT_EQ(result.output.find("900.00 USD"), std::string::npos);
}

// ---------------------------------------------------------------------
// Persistence integration: save / load wiring, not persistence's own
// round-trip correctness (already covered by the 44 tests in
// tests/persistence/SessionStoreTest.cpp).
// ---------------------------------------------------------------------

TEST(CliEndToEndTest, ScriptRoundTripSaveThenLoadRestoresOriginalStateAndDiscardsLaterMutation) {
    const std::string snapshotPath = uniqueTempPath("round_trip_snapshot");
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 4000 --name Sales --type revenue\n"
        "account create-root --code 5000 --name COGS --type expense\n"
        "post --date 2026-01-01 --description sale --debit 1000:100.00 --credit 4000:100.00\n"
        "post --date 2026-01-01 --description cogs --debit 5000:40.00 --credit 1000:40.00\n"
        "computed define --name GrossProfit --formula \"#4000 - #5000\"\n"
        "save " + snapshotPath + "\n"
        // Mutate the live session further -- this must be discarded by
        // the load below, not reflected in any report that follows it.
        "post --date 2026-01-02 --description later-mutation --debit 1000:9000.00 --credit 4000:9000.00\n"
        "load " + snapshotPath + "\n"
        "trial-balance\n"
        "balance-sheet\n"
        "income-statement\n"
        "computed eval GrossProfit\n");
    std::remove(snapshotPath.c_str());

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("Saved LedgerCore session to"), std::string::npos);
    EXPECT_NE(result.output.find("Loaded LedgerCore session from"), std::string::npos);

    // The mutation posted after save() must not appear anywhere in the
    // reports generated after load() restored the earlier state.
    EXPECT_EQ(result.output.find("9000.00 USD"), std::string::npos);
    EXPECT_EQ(result.output.find("later-mutation"), std::string::npos);

    // The originally-saved state (60.00 = 100.00 revenue - 40.00 expense)
    // is what every report reflects.
    EXPECT_NE(result.output.find("60.00 USD"), std::string::npos);
}

TEST(CliEndToEndTest, SaveWithNonexistentDirectoryExitsWithCodeOne) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "save /nonexistent-directory-for-ledgercore-tests/session.snapshot\n");

    EXPECT_EQ(result.exitCode, 1);
}

TEST(CliEndToEndTest, ScriptLoadOfMissingSnapshotExitsWithCodeOneAndAbortsScript) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "load /nonexistent-directory-for-ledgercore-tests/missing.snapshot\n"
        "trial-balance\n");

    EXPECT_EQ(result.exitCode, 1);
    // The script aborted at the failed load -- the trial-balance line
    // after it never ran.
    EXPECT_EQ(result.output.find("Trial Balance"), std::string::npos);
}

TEST(CliEndToEndTest, ReplSaveThenLoadThenTrialBalanceSucceeds) {
    const std::string snapshotPath = uniqueTempPath("repl_round_trip_snapshot");
    const RunResult result = runRepl(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 4000 --name Sales --type revenue\n"
        "post --date 2026-02-01 --description sale --debit 1000:75.00 --credit 4000:75.00\n"
        "save " + snapshotPath + "\n"
        "load " + snapshotPath + "\n"
        "trial-balance\n"
        "exit\n");
    std::remove(snapshotPath.c_str());

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("Saved LedgerCore session to"), std::string::npos);
    EXPECT_NE(result.output.find("Loaded LedgerCore session from"), std::string::npos);
    EXPECT_NE(result.output.find("75.00 USD"), std::string::npos);
}

TEST(CliEndToEndTest, ReplFailedLoadPrintsErrorAndSessionRemainsUsable) {
    const RunResult result = runRepl(
        "account create-root --code 1000 --name Cash --type asset\n"
        "load /nonexistent-directory-for-ledgercore-tests/missing.snapshot\n"
        "trial-balance\n"
        "exit\n");

    // The REPL keeps running past the failed load (exit 0, from the
    // explicit 'exit' command) and the pre-existing account is still
    // visible in the trial-balance printed afterward.
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("error:"), std::string::npos);
    EXPECT_NE(result.output.find("1000"), std::string::npos);
    EXPECT_NE(result.output.find("TOTAL"), std::string::npos);
}

// ---------------------------------------------------------------------
// Phase 14 regressions
// ---------------------------------------------------------------------

TEST(CliEndToEndTest, CreateChildUnderPostedAccountExitsWithCodeTwo) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 3000 --name Capital --type equity\n"
        "post --date 2026-01-01 --description \"Owner investment\" --debit 1000:500.00 --credit 3000:500.00\n"
        "account create-child --parent 1000 --code 1010 --name \"Petty cash\"\n"
        "trial-balance\n");

    EXPECT_EQ(result.exitCode, 2);
    EXPECT_NE(result.output.find("has posting history"), std::string::npos);
    EXPECT_EQ(result.output.find("created child account"), std::string::npos);
}

TEST(CliEndToEndTest, ReplRejectedChildUnderPostedAccountKeepsSessionSaveableAndReloadable) {
    // The exact Phase 13 reproduction: previously create-child succeeded,
    // trial-balance then failed as unbalanced, and the saved snapshot
    // could not be reloaded. Now create-child is rejected, and every
    // later step succeeds against an intact session.
    const std::string snapshotPath = uniqueTempPath("posted_leaf_snapshot");
    const RunResult result = runRepl(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 3000 --name Capital --type equity\n"
        "post --date 2026-01-01 --description \"Owner investment\" --debit 1000:500.00 --credit 3000:500.00\n"
        "account create-child --parent 1000 --code 1010 --name \"Petty cash\"\n"
        "save " + snapshotPath + "\n"
        "load " + snapshotPath + "\n"
        "trial-balance\n"
        "account show 1000\n"
        "exit\n");
    std::remove(snapshotPath.c_str());

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("has posting history"), std::string::npos);
    EXPECT_EQ(result.output.find("created child account"), std::string::npos);
    EXPECT_NE(result.output.find("Saved LedgerCore session to"), std::string::npos);
    EXPECT_NE(result.output.find("Loaded LedgerCore session from"), std::string::npos);
    EXPECT_EQ(result.output.find("does not balance"), std::string::npos);
    EXPECT_EQ(result.output.find("non-leaf"), std::string::npos);
    EXPECT_NE(result.output.find("500.00 USD"), std::string::npos);
    EXPECT_NE(result.output.find("kind:   leaf"), std::string::npos);
}

TEST(CliEndToEndTest, CreateChildUnderUnpostedAccountStillSucceeds) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Assets --type asset\n"
        "account create-root --code 3000 --name Capital --type equity\n"
        "account create-child --parent 1000 --code 1010 --name Cash\n"
        "post --date 2026-01-01 --description invest --debit 1010:500.00 --credit 3000:500.00\n"
        "account create-child --parent 1000 --code 1020 --name Bank\n"
        "trial-balance\n");

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("created child account 1020"), std::string::npos);
    EXPECT_NE(result.output.find("TOTAL"), std::string::npos);
}

TEST(CliEndToEndTest, PostWithDateOutsideSupportedRangeExitsWithCodeOne) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 3000 --name Capital --type equity\n"
        "post --date 2300-01-01 --description \"Far future\" --debit 1000:1.00 --credit 3000:1.00\n");

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_NE(result.output.find("date out of supported range"), std::string::npos);
    EXPECT_EQ(result.output.find("posted entry"), std::string::npos);
}

// ---------------------------------------------------------------------
// Phase 15 regressions
// ---------------------------------------------------------------------

TEST(CliEndToEndTest, PostWithImpossibleCalendarDateExitsWithCodeOne) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 3000 --name Capital --type equity\n"
        "post --date 2026-02-31 --description \"Not a real day\" --debit 1000:1.00 --credit 3000:1.00\n");

    EXPECT_EQ(result.exitCode, 1);
    EXPECT_NE(result.output.find("invalid calendar date: '2026-02-31'"), std::string::npos);
    EXPECT_EQ(result.output.find("posted entry"), std::string::npos);
}

TEST(CliEndToEndTest, BalanceSheetShowsUnclosedNetIncomeAndBalancingTotal) {
    // Assets 1,000 + 150 - 40 = 1,110.00; Liabilities 200.00 (loan);
    // Equity 800.00; Net Income 150.00 - 40.00 = 110.00.
    // Liabilities + Equity + Net Income = 1,110.00 == Total Assets.
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 2000 --name Loan --type liability\n"
        "account create-root --code 3000 --name Capital --type equity\n"
        "account create-root --code 4000 --name Sales --type revenue\n"
        "account create-root --code 5000 --name Rent --type expense\n"
        "post --date 2026-01-01 --description invest --debit 1000:800.00 --credit 3000:800.00\n"
        "post --date 2026-01-02 --description borrow --debit 1000:200.00 --credit 2000:200.00\n"
        "post --date 2026-01-03 --description sale --debit 1000:150.00 --credit 4000:150.00\n"
        "post --date 2026-01-04 --description rent --debit 5000:40.00 --credit 1000:40.00\n"
        "balance-sheet\n");

    ASSERT_EQ(result.exitCode, 0) << result.output;
    // Only the report itself -- not the earlier "created root account"
    // echo lines -- is inspected below.
    const std::size_t report = result.output.find("Balance Sheet (USD)");
    ASSERT_NE(report, std::string::npos);
    const std::string out = result.output.substr(report);
    const auto lineAt = [&out](std::size_t pos) { return out.substr(pos, out.find('\n', pos) - pos); };
    const std::size_t assets = out.find("Assets:");
    const std::size_t liabilities = out.find("Liabilities:");
    const std::size_t equity = out.find("Equity:");
    const std::size_t earnings = out.find("Current-period earnings:");
    const std::size_t netIncome = out.find("Net Income (unclosed)");
    const std::size_t total = out.find("Liabilities + Equity + Net Income");
    ASSERT_NE(assets, std::string::npos);
    ASSERT_NE(liabilities, std::string::npos);
    ASSERT_NE(equity, std::string::npos);
    ASSERT_NE(earnings, std::string::npos);
    ASSERT_NE(netIncome, std::string::npos);
    ASSERT_NE(total, std::string::npos);
    EXPECT_LT(assets, liabilities);
    EXPECT_LT(liabilities, equity);
    EXPECT_LT(equity, earnings);
    EXPECT_LT(earnings, netIncome);
    EXPECT_LT(netIncome, total);

    // Section totals, then net income, then the balancing total -- which
    // equals the Assets total exactly.
    EXPECT_NE(out.find("1110.00 USD", assets), std::string::npos);
    EXPECT_NE(out.find("200.00 USD", liabilities), std::string::npos);
    EXPECT_NE(out.find("800.00 USD", equity), std::string::npos);
    const std::string netIncomeLine = lineAt(netIncome);
    EXPECT_NE(netIncomeLine.find(" 110.00 USD"), std::string::npos) << netIncomeLine;
    const std::string totalLine = lineAt(total);
    EXPECT_NE(totalLine.find("1110.00 USD"), std::string::npos) << totalLine;
    // Revenue/Expense accounts still never appear as Balance Sheet lines.
    EXPECT_EQ(out.find("Sales"), std::string::npos);
    EXPECT_EQ(out.find("Rent"), std::string::npos);
}

TEST(CliEndToEndTest, BalanceSheetAsOfShowsNetIncomeOnlyUpToCutoff) {
    const RunResult result = runScript(
        "account create-root --code 1000 --name Cash --type asset\n"
        "account create-root --code 3000 --name Capital --type equity\n"
        "account create-root --code 4000 --name Sales --type revenue\n"
        "post --date 2026-01-01 --description invest --debit 1000:500.00 --credit 3000:500.00\n"
        "post --date 2026-02-01 --description sale --debit 1000:30.00 --credit 4000:30.00\n"
        "post --date 2026-03-01 --description later-sale --debit 1000:70.00 --credit 4000:70.00\n"
        "balance-sheet --as-of 2026-03-01\n");

    ASSERT_EQ(result.exitCode, 0) << result.output;
    const std::size_t netIncome = result.output.find("Net Income (unclosed)");
    ASSERT_NE(netIncome, std::string::npos);
    const std::string netIncomeLine =
        result.output.substr(netIncome, result.output.find('\n', netIncome) - netIncome);
    EXPECT_NE(netIncomeLine.find("30.00 USD"), std::string::npos) << netIncomeLine;
    const std::size_t total = result.output.find("Liabilities + Equity + Net Income");
    ASSERT_NE(total, std::string::npos);
    EXPECT_NE(result.output.find("530.00 USD", total), std::string::npos);
    EXPECT_EQ(result.output.find("100.00 USD"), std::string::npos);
}

// ---------------------------------------------------------------------
// Phase 17: close
// ---------------------------------------------------------------------

namespace {

const char* const kYearOneScript =
    "account create-root --code 1000 --name Cash --type asset\n"
    "account create-root --code 3000 --name Capital --type equity\n"
    "account create-root --code 3100 --name RetainedEarnings --type equity\n"
    "account create-root --code 4000 --name Sales --type revenue\n"
    "account create-root --code 5000 --name Rent --type expense\n"
    "post --date 2026-01-01 --description invest --debit 1000:1000.00 --credit 3000:1000.00\n"
    "post --date 2026-03-01 --description sale --debit 1000:700.00 --credit 4000:700.00\n"
    "post --date 2026-06-01 --description rent --debit 5000:250.00 --credit 1000:250.00\n";

std::string lineContaining(const std::string& out, const std::string& needle, std::size_t from = 0) {
    const std::size_t pos = out.find(needle, from);
    if (pos == std::string::npos) {
        return "";
    }
    const std::size_t start = out.rfind('\n', pos) == std::string::npos ? 0 : out.rfind('\n', pos) + 1;
    return out.substr(start, out.find('\n', pos) - start);
}

} // namespace

TEST(CliEndToEndTest, CloseMovesNetIncomeIntoRetainedEarningsWithoutErasingTheIncomeStatement) {
    const RunResult result = runScript(std::string(kYearOneScript)
                                       + "close --retained-earnings 3100 --as-of 2027-01-01\n"
                                         "trial-balance\n"
                                         "balance-sheet\n"
                                         "income-statement --from 2026-01-01 --to 2027-01-01\n");

    ASSERT_EQ(result.exitCode, 0) << result.output;
    const std::string& out = result.output;
    EXPECT_NE(out.find("posted closing entry #4 into 3100 as of 2027-01-01 (net income 450.00 USD)"),
              std::string::npos)
        << out;

    // Post-closing trial balance: temporary accounts zero, retained earnings 450.
    const std::size_t tb = out.find("Trial Balance (USD)");
    ASSERT_NE(tb, std::string::npos);
    EXPECT_NE(lineContaining(out, "RetainedEarnings", tb).find("450.00 USD"), std::string::npos);
    EXPECT_NE(lineContaining(out, "4000      Sales", tb).find("0.00 USD          0.00 USD"), std::string::npos);

    // Balance sheet: retained earnings in equity, nothing left unclosed, still balanced.
    const std::size_t bs = out.find("Balance Sheet (USD)");
    ASSERT_NE(bs, std::string::npos);
    EXPECT_NE(lineContaining(out, "Net Income (unclosed)", bs).find("0.00 USD"), std::string::npos);
    EXPECT_NE(lineContaining(out, "Liabilities + Equity + Net Income", bs).find("1450.00 USD"), std::string::npos);

    // The closed year's income statement still reports its result.
    const std::size_t is = out.find("Income Statement (USD)");
    ASSERT_NE(is, std::string::npos);
    EXPECT_NE(lineContaining(out, "Net Income", is).find("450.00 USD"), std::string::npos);
}

TEST(CliEndToEndTest, ClosingTheSameDateTwiceFailsWithAccountingErrorCode) {
    const RunResult result = runScript(std::string(kYearOneScript)
                                       + "close --retained-earnings 3100 --as-of 2027-01-01\n"
                                         "close --retained-earnings 3100 --as-of 2027-01-01\n");

    EXPECT_EQ(result.exitCode, 2);
    EXPECT_NE(result.output.find("posted closing entry #4"), std::string::npos);
    EXPECT_NE(result.output.find("No Revenue or Expense account has a non-zero balance to close"),
              std::string::npos);
    EXPECT_EQ(result.output.find("posted closing entry #5"), std::string::npos);
}

TEST(CliEndToEndTest, CloseIntoNonEquityOrUnknownAccountIsRejected) {
    const RunResult asset =
        runScript(std::string(kYearOneScript) + "close --retained-earnings 1000 --as-of 2027-01-01\n");
    EXPECT_EQ(asset.exitCode, 2);
    EXPECT_NE(asset.output.find("must be an Equity account"), std::string::npos);

    const RunResult unknown =
        runScript(std::string(kYearOneScript) + "close --retained-earnings 9999 --as-of 2027-01-01\n");
    EXPECT_EQ(unknown.exitCode, 2);
    EXPECT_NE(unknown.output.find("No account found"), std::string::npos);

    const RunResult badDate =
        runScript(std::string(kYearOneScript) + "close --retained-earnings 3100 --as-of 2026-02-31\n");
    EXPECT_EQ(badDate.exitCode, 1);
}

TEST(CliEndToEndTest, ClosedSessionSavesAsV2AndReloadsWithSameReports) {
    const std::string snapshotPath = uniqueTempPath("closed_snapshot");
    const RunResult result = runRepl(std::string(kYearOneScript)
                                     + "close --retained-earnings 3100 --as-of 2027-01-01\n"
                                       "save " + snapshotPath + "\n"
                                       "load " + snapshotPath + "\n"
                                       "income-statement --from 2026-01-01 --to 2027-01-01\n"
                                       "trial-balance\n"
                                       "exit\n");
    std::ifstream snapshot(snapshotPath);
    std::string header;
    std::getline(snapshot, header);
    std::remove(snapshotPath.c_str());

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(header, "LEDGERCORE-SNAPSHOT v2");
    EXPECT_EQ(result.output.find("error:"), std::string::npos) << result.output;
    const std::size_t is = result.output.find("Income Statement (USD)");
    ASSERT_NE(is, std::string::npos);
    EXPECT_NE(lineContaining(result.output, "Net Income", is).find("450.00 USD"), std::string::npos);
    EXPECT_NE(lineContaining(result.output, "RetainedEarnings", is).find("450.00 USD"), std::string::npos);
}

// ---------------------------------------------------------------------
// Phase 18: accounting periods
// ---------------------------------------------------------------------

TEST(CliEndToEndTest, PeriodCreateCloseListAndBackdatedPostingRejected) {
    const RunResult result = runScript(std::string(kYearOneScript)
                                       + "period create --start 2026-01-01 --end 2027-01-01\n"
                                         "period create --start 2027-01-01 --end 2028-01-01\n"
                                         "close --retained-earnings 3100 --as-of 2027-01-01\n"
                                         "period close --start 2026-01-01 --end 2027-01-01\n"
                                         "period list\n"
                                         "post --date 2027-01-01 --description next-year --debit 1000:5.00 --credit 4000:5.00\n"
                                         "post --date 2026-12-31 --description backdated --debit 1000:5.00 --credit 4000:5.00\n");

    EXPECT_EQ(result.exitCode, 2) << result.output;
    const std::string& out = result.output;
    EXPECT_NE(out.find("created accounting period [2026-01-01, 2027-01-01) (open)"), std::string::npos) << out;
    EXPECT_NE(out.find("posted closing entry"), std::string::npos);
    EXPECT_NE(out.find("closed accounting period [2026-01-01, 2027-01-01)"), std::string::npos);
    EXPECT_NE(out.find("2026-01-01  2027-01-01  closed"), std::string::npos) << out;
    EXPECT_NE(out.find("2027-01-01  2028-01-01  open"), std::string::npos) << out;
    EXPECT_NE(out.find("posted entry #5"), std::string::npos);  // dated at the closed period's end: allowed
    EXPECT_NE(out.find("Cannot post an entry dated 2026-12-31 into closed accounting period "
                       "[2026-01-01, 2027-01-01)"),
              std::string::npos)
        << out;
    EXPECT_EQ(out.find("posted entry #6"), std::string::npos);
}

TEST(CliEndToEndTest, PeriodListWithNoPeriods) {
    const RunResult result = runScript("period list\n");
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_NE(result.output.find("no accounting periods defined"), std::string::npos);
}

TEST(CliEndToEndTest, InvalidPeriodLifecycleOperationsUseAccountingExitCode) {
    const RunResult closeUndefined = runScript("period close --start 2026-01-01 --end 2027-01-01\n");
    EXPECT_EQ(closeUndefined.exitCode, 2);
    EXPECT_NE(closeUndefined.output.find("No accounting period is defined"), std::string::npos);

    const RunResult closeTwice = runScript("period create --start 2026-01-01 --end 2027-01-01\n"
                                           "period close --start 2026-01-01 --end 2027-01-01\n"
                                           "period close --start 2026-01-01 --end 2027-01-01\n");
    EXPECT_EQ(closeTwice.exitCode, 2);
    EXPECT_NE(closeTwice.output.find("already closed"), std::string::npos);

    const RunResult overlap = runScript("period create --start 2026-01-01 --end 2027-01-01\n"
                                        "period create --start 2026-06-01 --end 2027-06-01\n");
    EXPECT_EQ(overlap.exitCode, 2);
    EXPECT_NE(overlap.output.find("overlaps"), std::string::npos);

    const RunResult backwards = runScript("period create --start 2027-01-01 --end 2026-01-01\n");
    EXPECT_EQ(backwards.exitCode, 2);
}

TEST(CliEndToEndTest, InvalidPeriodDatesUseUsageExitCode) {
    EXPECT_EQ(runScript("period create --start 2026-02-30 --end 2027-01-01\n").exitCode, 1);
    EXPECT_EQ(runScript("period create --start 2026-01-01 --end 2300-01-01\n").exitCode, 1);
    EXPECT_EQ(runScript("period create --start 2026-01-01\n").exitCode, 1);
}

TEST(CliEndToEndTest, ClosedPeriodSurvivesSaveAndLoad) {
    const std::string snapshotPath = uniqueTempPath("period_snapshot");
    const RunResult result = runRepl(std::string(kYearOneScript)
                                     + "period create --start 2026-01-01 --end 2027-01-01\n"
                                       "period close --start 2026-01-01 --end 2027-01-01\n"
                                       "save " + snapshotPath + "\n"
                                       "load " + snapshotPath + "\n"
                                       "period list\n"
                                       "post --date 2026-06-15 --description backdated --debit 1000:1.00 --credit 4000:1.00\n"
                                       "exit\n");
    std::ifstream snapshot(snapshotPath);
    std::string header;
    std::getline(snapshot, header);
    std::remove(snapshotPath.c_str());

    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(header, "LEDGERCORE-SNAPSHOT v3");
    EXPECT_NE(result.output.find("Loaded LedgerCore session from"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("2026-01-01  2027-01-01  closed"), std::string::npos) << result.output;
    EXPECT_NE(result.output.find("into closed accounting period"), std::string::npos);
}

// ---------------------------------------------------------------------
// Phase 19: journal
// ---------------------------------------------------------------------

namespace {

// kYearOneScript (#1 invest, #2 sale 700, #3 rent 250), then a close
// (#4) and a next-year sale (#5).
std::string journalScript(const std::string& journalCommand) {
    return std::string(kYearOneScript)
           + "close --retained-earnings 3100 --as-of 2027-01-01\n"
             "post --date 2027-02-01 --description \"Next year\" --debit 1000:12.34 --credit 4000:12.34\n"
           + journalCommand + "\n";
}

std::string journalSection(const std::string& output) {
    const std::size_t pos = output.find("Journal (USD)");
    return pos == std::string::npos ? "" : output.substr(pos);
}

} // namespace

TEST(CliEndToEndTest, JournalListsEveryEntryWithLinesInPostingOrder) {
    const RunResult result = runScript(journalScript("journal"));
    ASSERT_EQ(result.exitCode, 0) << result.output;
    const std::string journal = journalSection(result.output);
    EXPECT_EQ(journal.rfind("Journal (USD): 5 entries\n", 0), 0u) << journal;
    EXPECT_NE(journal.find("#1  2026-01-01  standard  \"invest\"\n"
                           "  DEBIT   1000      Cash                           1000.00 USD\n"
                           "  CREDIT  3000      Capital                        1000.00 USD\n"),
              std::string::npos)
        << journal;
    EXPECT_NE(journal.find("#4  2026-12-31T23:59:59.999999000Z  closing  \"Closing entry\"\n"), std::string::npos)
        << journal;
    EXPECT_NE(journal.find("  CREDIT  3100      RetainedEarnings                450.00 USD\n"), std::string::npos);
    EXPECT_NE(journal.find("12.34 USD"), std::string::npos);
    EXPECT_LT(journal.find("#1 "), journal.find("#2 "));
    EXPECT_LT(journal.find("#4 "), journal.find("#5 "));
}

TEST(CliEndToEndTest, JournalDateFilterUsesHalfOpenRange) {
    const RunResult result = runScript(journalScript("journal --from 2026-03-01 --to 2026-06-01"));
    ASSERT_EQ(result.exitCode, 0) << result.output;
    const std::string journal = journalSection(result.output);
    EXPECT_EQ(journal.rfind("Journal (USD): 1 entry\n", 0), 0u) << journal;  // sale on 03-01; rent on 06-01 excluded
    EXPECT_NE(journal.find("#2  2026-03-01  standard  \"sale\""), std::string::npos);
    EXPECT_EQ(journal.find("#3 "), std::string::npos);
}

TEST(CliEndToEndTest, JournalAccountFilterMatchesDebitAndCreditLines) {
    const RunResult result = runScript(journalScript("journal --account 5000"));
    ASSERT_EQ(result.exitCode, 0) << result.output;
    const std::string journal = journalSection(result.output);
    EXPECT_EQ(journal.rfind("Journal (USD): 2 entries\n", 0), 0u) << journal;  // rent (debit) + close (credit)
    EXPECT_NE(journal.find("#3 "), std::string::npos);
    EXPECT_NE(journal.find("#4 "), std::string::npos);
}

TEST(CliEndToEndTest, JournalKindFilters) {
    const RunResult closing = runScript(journalScript("journal --closing"));
    ASSERT_EQ(closing.exitCode, 0) << closing.output;
    EXPECT_EQ(journalSection(closing.output).rfind("Journal (USD): 1 entry\n#4 ", 0), 0u) << closing.output;

    const RunResult standard = runScript(journalScript("journal --standard --account 4000"));
    ASSERT_EQ(standard.exitCode, 0) << standard.output;
    const std::string journal = journalSection(standard.output);
    EXPECT_EQ(journal.rfind("Journal (USD): 2 entries\n", 0), 0u) << journal;  // #2 and #5, not the close
    EXPECT_EQ(journal.find("closing"), std::string::npos);
}

TEST(CliEndToEndTest, JournalWithNoMatchesPrintsAnEmptyResult) {
    const RunResult result = runScript(journalScript("journal --from 2030-01-01 --to 2031-01-01"));
    EXPECT_EQ(result.exitCode, 0);
    EXPECT_EQ(journalSection(result.output), "Journal (USD): 0 entries\n");
}

TEST(CliEndToEndTest, JournalRejectsInvalidFiltersWithUsageExitCode) {
    EXPECT_EQ(runScript(journalScript("journal --standard --closing")).exitCode, 1);
    EXPECT_EQ(runScript(journalScript("journal --from 2026-01-01")).exitCode, 1);
    EXPECT_EQ(runScript(journalScript("journal --from 2026-02-30 --to 2027-01-01")).exitCode, 1);
    EXPECT_EQ(runScript(journalScript("journal --bogus")).exitCode, 1);
    const RunResult unknownAccount = runScript(journalScript("journal --account 9999"));
    EXPECT_EQ(unknownAccount.exitCode, 1);
    EXPECT_NE(unknownAccount.output.find("no such account: '9999'"), std::string::npos);
    // An inverted range is the existing domain Period rule (accounting exit code).
    EXPECT_EQ(runScript(journalScript("journal --from 2027-01-01 --to 2026-01-01")).exitCode, 2);
}

// ---------------------------------------------------------------------
// Phase 20: adversarial snapshots fail cleanly, never crash the CLI
// ---------------------------------------------------------------------

TEST(CliEndToEndTest, LoadingA100000LevelSnapshotFailsCleanlyAndTheSessionSurvives) {
    const std::string snapshotPath = uniqueTempPath("deep_snapshot");
    {
        std::ofstream out(snapshotPath);
        out << "LEDGERCORE-SNAPSHOT v1\nCURRENCY USD\nACCOUNT ROOT D1 Asset \"Deep\"\n";
        for (int level = 2; level <= 100000; ++level) {
            out << "ACCOUNT CHILD D" << level - 1 << " D" << level << " \"Deep\"\n";
        }
    }
    const RunResult script = runScript("load " + snapshotPath + "\n");
    EXPECT_EQ(script.exitCode, 2);
    EXPECT_NE(script.output.find("the account tree may be at most 1000 levels deep"), std::string::npos)
        << script.output;

    const RunResult repl = runRepl("account create-root --code 1000 --name Cash --type asset\n"
                                   "load " + snapshotPath + "\n"
                                   "trial-balance\n"
                                   "save " + snapshotPath + ".after\n"
                                   "exit\n");
    std::remove(snapshotPath.c_str());
    std::remove((snapshotPath + ".after").c_str());
    EXPECT_EQ(repl.exitCode, 0);
    EXPECT_NE(repl.output.find("levels deep"), std::string::npos);
    EXPECT_NE(repl.output.find("TOTAL"), std::string::npos);  // the original session still works
    EXPECT_NE(repl.output.find("Saved LedgerCore session"), std::string::npos);
}

TEST(CliEndToEndTest, AdversariallyDeepFormulaIsRejectedWithAccountingExitCode) {
    const RunResult result = runScript("formula eval " + std::string(100000, '(') + "1" + std::string(100000, ')')
                                       + "\n");
    EXPECT_EQ(result.exitCode, 2);
    EXPECT_NE(result.output.find("nested more than 128 levels deep"), std::string::npos);
}

TEST(CliEndToEndTest, SnapshotBeyondTheClosingEntryCapFailsWithAccountingExitCode) {
    const std::string snapshotPath = uniqueTempPath("closing_cap_snapshot");
    {
        std::ofstream out(snapshotPath);
        out << "LEDGERCORE-SNAPSHOT v2\nCURRENCY USD\nACCOUNT ROOT 1000 Asset \"Cash\"\n"
               "ACCOUNT ROOT 3100 Equity \"RE\"\nACCOUNT ROOT 4000 Revenue \"Sales\"\n";
        const long long base = 1798761600LL * 1000000000LL;
        for (long long i = 0; i < 1001; ++i) {
            out << "ENTRY " << base + i * 2000000000LL << " \"s\"\n  DEBIT 1000 100\n  CREDIT 4000 100\n";
            out << "CLOSING " << base + i * 2000000000LL + 1000000000LL << " \"c\"\n  DEBIT 4000 100\n  CREDIT 3100 100\n";
        }
    }
    const RunResult result = runScript("load " + snapshotPath + "\n");
    std::remove(snapshotPath.c_str());
    EXPECT_EQ(result.exitCode, 2);
    EXPECT_NE(result.output.find("A Ledger may hold at most 1000 closing entries"), std::string::npos) << result.output;
}

TEST(CliEndToEndTest, ComputedEvaluationBudgetFailureUsesAccountingExitCode) {
    std::string script = "account create-root --code 1000 --name Cash --type asset\n"
                         "computed define --name C0 --formula \"#1000\"\n";
    for (int i = 1; i <= 300; ++i) {
        script += "computed define --name C" + std::to_string(i) + " --formula \"@C" + std::to_string(i - 1) + "\"\n";
    }
    script += "computed eval C300\n";
    const RunResult result = runScript(script);
    EXPECT_EQ(result.exitCode, 2);
    EXPECT_NE(result.output.find("evaluation limit of 256 levels"), std::string::npos);
}
