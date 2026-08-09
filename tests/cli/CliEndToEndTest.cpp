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
