#include "CommandParser.h"

#include <gtest/gtest.h>

namespace {

using ledgercore::cli::CliUsageError;
using ledgercore::cli::CommandKind;
using ledgercore::cli::ParsedCommand;
using ledgercore::cli::parseLine;

TEST(CommandParserTest, ValidAccountCreateRootCommand) {
    const std::optional<ParsedCommand> pc = parseLine("account create-root --code 1000 --name Cash --type asset");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::AccountCreateRoot);
    EXPECT_EQ(pc->code, "1000");
    EXPECT_EQ(pc->name, "Cash");
    EXPECT_EQ(pc->type, "asset");
}

TEST(CommandParserTest, ValidAccountCreateChildCommand) {
    const std::optional<ParsedCommand> pc =
        parseLine("account create-child --parent 1000 --code 1010 --name \"Petty Cash\"");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::AccountCreateChild);
    EXPECT_EQ(pc->parentCode, "1000");
    EXPECT_EQ(pc->code, "1010");
    EXPECT_EQ(pc->name, "Petty Cash");
}

TEST(CommandParserTest, ValidAccountListWithTreeFlag) {
    const std::optional<ParsedCommand> pc = parseLine("account list --tree");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::AccountList);
    EXPECT_TRUE(pc->tree);
}

TEST(CommandParserTest, ValidAccountListWithoutTreeFlag) {
    const std::optional<ParsedCommand> pc = parseLine("account list");
    ASSERT_TRUE(pc.has_value());
    EXPECT_FALSE(pc->tree);
}

TEST(CommandParserTest, ValidAccountShowCommand) {
    const std::optional<ParsedCommand> pc = parseLine("account show 1000");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::AccountShow);
    EXPECT_EQ(pc->code, "1000");
}

TEST(CommandParserTest, ValidPostCommandWithMultipleLines) {
    const std::optional<ParsedCommand> pc = parseLine(
        "post --date 2026-04-15 --description \"Sold widgets\" --debit 1000:250.00 --credit 4000:200.00 --credit "
        "4100:50.00");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::Post);
    EXPECT_EQ(pc->date, "2026-04-15");
    EXPECT_EQ(pc->description, "Sold widgets");
    ASSERT_EQ(pc->lines.size(), 3u);
    EXPECT_EQ(pc->lines[0].accountCode, "1000");
    EXPECT_EQ(pc->lines[0].amountText, "250.00");
    EXPECT_TRUE(pc->lines[0].isDebit);
    EXPECT_EQ(pc->lines[1].accountCode, "4000");
    EXPECT_FALSE(pc->lines[1].isDebit);
    EXPECT_EQ(pc->lines[2].accountCode, "4100");
    EXPECT_FALSE(pc->lines[2].isDebit);
}

TEST(CommandParserTest, MissingRequiredOptionThrows) {
    EXPECT_THROW(parseLine("account create-root --code 1000 --name Cash"), CliUsageError);
}

TEST(CommandParserTest, MissingRequiredOptionOnPostThrows) {
    EXPECT_THROW(parseLine("post --description \"no date\" --debit 1000:10.00 --credit 2000:10.00"), CliUsageError);
}

TEST(CommandParserTest, UnknownOptionThrows) {
    EXPECT_THROW(parseLine("account create-root --code 1000 --name Cash --type asset --currency USD"),
                 CliUsageError);
}

TEST(CommandParserTest, UnknownOptionOnBalanceSheetThrows) {
    EXPECT_THROW(parseLine("balance-sheet --from 2026-01-01 --to 2026-02-01"), CliUsageError);
}

TEST(CommandParserTest, MalformedDebitArgumentMissingColonThrows) {
    EXPECT_THROW(parseLine("post --date 2026-01-01 --description x --debit 1000 --credit 2000:10.00"),
                 CliUsageError);
}

TEST(CommandParserTest, MalformedDebitArgumentEmptyCodeThrows) {
    EXPECT_THROW(parseLine("post --date 2026-01-01 --description x --debit :10.00 --credit 2000:10.00"),
                 CliUsageError);
}

TEST(CommandParserTest, MalformedDebitArgumentEmptyAmountThrows) {
    EXPECT_THROW(parseLine("post --date 2026-01-01 --description x --debit 1000: --credit 2000:10.00"),
                 CliUsageError);
}

TEST(CommandParserTest, MutuallyExclusivePeriodFlagsOnTrialBalanceThrows) {
    EXPECT_THROW(parseLine("trial-balance --as-of 2026-01-01 --from 2026-01-01 --to 2026-02-01"), CliUsageError);
}

TEST(CommandParserTest, FromWithoutToOnTrialBalanceThrows) {
    EXPECT_THROW(parseLine("trial-balance --from 2026-01-01"), CliUsageError);
}

TEST(CommandParserTest, FromWithoutToOnIncomeStatementThrows) {
    EXPECT_THROW(parseLine("income-statement --from 2026-01-01"), CliUsageError);
}

TEST(CommandParserTest, InvalidCommandThrows) {
    EXPECT_THROW(parseLine("not-a-real-command"), CliUsageError);
}

TEST(CommandParserTest, InvalidAccountSubcommandThrows) {
    EXPECT_THROW(parseLine("account not-a-subcommand"), CliUsageError);
}

TEST(CommandParserTest, BlankLineIsSkipped) {
    EXPECT_EQ(parseLine(""), std::nullopt);
    EXPECT_EQ(parseLine("   "), std::nullopt);
}

TEST(CommandParserTest, CommentLineIsSkipped) {
    EXPECT_EQ(parseLine("# this is a comment"), std::nullopt);
}

TEST(CommandParserTest, ExitAndQuitAreRecognized) {
    const std::optional<ParsedCommand> exitCmd = parseLine("exit");
    const std::optional<ParsedCommand> quitCmd = parseLine("quit");
    ASSERT_TRUE(exitCmd.has_value());
    ASSERT_TRUE(quitCmd.has_value());
    EXPECT_EQ(exitCmd->kind, CommandKind::Exit);
    EXPECT_EQ(quitCmd->kind, CommandKind::Exit);
}

TEST(CommandParserTest, ValidTrialBalanceWithAsOf) {
    const std::optional<ParsedCommand> pc = parseLine("trial-balance --as-of 2026-05-01");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->asOf, "2026-05-01");
    EXPECT_TRUE(pc->from.empty());
}

TEST(CommandParserTest, ValidComputedDefineCommand) {
    const std::optional<ParsedCommand> pc = parseLine("computed define --name GrossProfit --formula \"#4000 - #5000\"");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::ComputedDefine);
    EXPECT_EQ(pc->computedName, "GrossProfit");
    EXPECT_EQ(pc->expression, "#4000 - #5000");
}

TEST(CommandParserTest, ValidFormulaEvalCommandJoinsUnquotedTokens) {
    const std::optional<ParsedCommand> pc = parseLine("formula eval #4000 - #5000");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::FormulaEval);
    EXPECT_EQ(pc->expression, "#4000 - #5000");
}

// ---------------------------------------------------------------------
// save / load
// ---------------------------------------------------------------------

TEST(CommandParserTest, ValidSaveCommand) {
    const std::optional<ParsedCommand> pc = parseLine("save ledger.snapshot");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::Save);
    EXPECT_EQ(pc->path, "ledger.snapshot");
}

TEST(CommandParserTest, ValidLoadCommand) {
    const std::optional<ParsedCommand> pc = parseLine("load ledger.snapshot");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::Load);
    EXPECT_EQ(pc->path, "ledger.snapshot");
}

TEST(CommandParserTest, SaveCommandSupportsQuotedPathWithSpaces) {
    const std::optional<ParsedCommand> pc = parseLine("save \"my ledger.snapshot\"");
    ASSERT_TRUE(pc.has_value());
    EXPECT_EQ(pc->kind, CommandKind::Save);
    EXPECT_EQ(pc->path, "my ledger.snapshot");
}

TEST(CommandParserTest, SaveWithNoPathThrows) {
    EXPECT_THROW(parseLine("save"), CliUsageError);
}

TEST(CommandParserTest, LoadWithNoPathThrows) {
    EXPECT_THROW(parseLine("load"), CliUsageError);
}

TEST(CommandParserTest, SaveWithTwoPathsThrows) {
    EXPECT_THROW(parseLine("save a b"), CliUsageError);
}

TEST(CommandParserTest, LoadWithTwoPathsThrows) {
    EXPECT_THROW(parseLine("load a b"), CliUsageError);
}

TEST(CommandParserTest, SaveWithUnknownFlagThrows) {
    EXPECT_THROW(parseLine("save --unknown ledger.snapshot"), CliUsageError);
}

TEST(CommandParserTest, LoadWithUnknownFlagThrows) {
    EXPECT_THROW(parseLine("load --unknown ledger.snapshot"), CliUsageError);
}

} // namespace
