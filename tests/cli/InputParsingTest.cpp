#include "InputParsing.h"

#include <gtest/gtest.h>

#include "CommandParser.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/Money.h"

namespace {

using ledgercore::cli::CliUsageError;
using ledgercore::cli::parseAccountType;
using ledgercore::cli::parseAmount;
using ledgercore::cli::parseDate;
using ledgercore::domain::AccountType;
using ledgercore::domain::Currency;
using ledgercore::domain::Money;

Currency usd() {
    return Currency("USD");
}

// ---------------------------------------------------------------------
// parseAccountType
// ---------------------------------------------------------------------

TEST(InputParsingTest, ValidAccountTypeAsset) {
    EXPECT_EQ(parseAccountType("asset"), AccountType::Asset);
}

TEST(InputParsingTest, ValidAccountTypeIsCaseInsensitive) {
    EXPECT_EQ(parseAccountType("Liability"), AccountType::Liability);
    EXPECT_EQ(parseAccountType("EQUITY"), AccountType::Equity);
    EXPECT_EQ(parseAccountType("Revenue"), AccountType::Revenue);
    EXPECT_EQ(parseAccountType("eXpEnSe"), AccountType::Expense);
}

TEST(InputParsingTest, InvalidAccountTypeThrowsCliUsageError) {
    EXPECT_THROW(parseAccountType("not-a-type"), CliUsageError);
}

TEST(InputParsingTest, EmptyAccountTypeThrowsCliUsageError) {
    EXPECT_THROW(parseAccountType(""), CliUsageError);
}

// ---------------------------------------------------------------------
// parseAmount
// ---------------------------------------------------------------------

TEST(InputParsingTest, ValidWholeDollarAmount) {
    const Money amount = parseAmount("250", usd());
    EXPECT_EQ(amount, Money::fromMajorUnits(250, 0, usd()));
}

TEST(InputParsingTest, ValidTwoDecimalAmount) {
    const Money amount = parseAmount("250.00", usd());
    EXPECT_EQ(amount, Money::fromMajorUnits(250, 0, usd()));
}

TEST(InputParsingTest, ValidOneDecimalAmountTreatedAsTenths) {
    const Money amount = parseAmount("5.1", usd());
    EXPECT_EQ(amount, Money::fromMajorUnits(5, 10, usd()));
}

TEST(InputParsingTest, NegativeAmountBelowOneDollar) {
    const Money amount = parseAmount("-0.10", usd());
    EXPECT_EQ(amount, Money::fromMajorUnits(0, -10, usd()));
}

TEST(InputParsingTest, NegativeAmountAboveOneDollar) {
    const Money amount = parseAmount("-12.34", usd());
    EXPECT_EQ(amount, Money::fromMajorUnits(-12, -34, usd()));
}

TEST(InputParsingTest, MalformedAmountTwoDecimalPointsThrowsCliUsageError) {
    EXPECT_THROW(parseAmount("12.3.4", usd()), CliUsageError);
}

TEST(InputParsingTest, MalformedAmountNonNumericTextThrowsCliUsageError) {
    EXPECT_THROW(parseAmount("abc", usd()), CliUsageError);
}

TEST(InputParsingTest, MalformedAmountEmptyStringThrowsCliUsageError) {
    EXPECT_THROW(parseAmount("", usd()), CliUsageError);
}

TEST(InputParsingTest, MalformedAmountTooManyFractionalDigitsThrowsCliUsageError) {
    EXPECT_THROW(parseAmount("12.345", usd()), CliUsageError);
}

TEST(InputParsingTest, MalformedAmountBareMinusSignThrowsCliUsageError) {
    EXPECT_THROW(parseAmount("-", usd()), CliUsageError);
}

TEST(InputParsingTest, MalformedAmountTrailingDotThrowsCliUsageError) {
    EXPECT_THROW(parseAmount("12.", usd()), CliUsageError);
}

TEST(InputParsingTest, AmountWithTooManyDigitsToParseThrowsCliUsageError) {
    // 20 digits overflows std::int64_t during InputParsing's own digit
    // accumulation, before Money::fromMajorUnits() is ever reached.
    EXPECT_THROW(parseAmount("99999999999999999999.00", usd()), CliUsageError);
}

// ---------------------------------------------------------------------
// parseDate
// ---------------------------------------------------------------------

TEST(InputParsingTest, ValidDateParses) {
    const auto date = parseDate("2026-04-15");
    const auto nextDay = parseDate("2026-04-16");
    EXPECT_LT(date, nextDay);
}

TEST(InputParsingTest, AdjacentDatesAreExactlyTwentyFourHoursApart) {
    const auto day1 = parseDate("2026-04-01");
    const auto day2 = parseDate("2026-04-02");
    EXPECT_EQ(day2 - day1, std::chrono::hours(24));
}

TEST(InputParsingTest, InvalidDateWrongSeparatorThrowsCliUsageError) {
    EXPECT_THROW(parseDate("2026/04/15"), CliUsageError);
}

TEST(InputParsingTest, InvalidDateWrongLengthThrowsCliUsageError) {
    EXPECT_THROW(parseDate("26-04-15"), CliUsageError);
}

TEST(InputParsingTest, InvalidDateNonNumericThrowsCliUsageError) {
    EXPECT_THROW(parseDate("YYYY-MM-DD"), CliUsageError);
}

TEST(InputParsingTest, InvalidDateMonthOutOfRangeThrowsCliUsageError) {
    EXPECT_THROW(parseDate("2026-13-01"), CliUsageError);
}

TEST(InputParsingTest, InvalidDateDayOutOfRangeThrowsCliUsageError) {
    EXPECT_THROW(parseDate("2026-04-32"), CliUsageError);
}

TEST(InputParsingTest, InvalidDateEmptyStringThrowsCliUsageError) {
    EXPECT_THROW(parseDate(""), CliUsageError);
}

} // namespace
