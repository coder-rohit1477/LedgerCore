#pragma once

#include <chrono>
#include <string>

#include "ledgercore/domain/AccountCode.h"
#include "ledgercore/domain/AccountType.h"
#include "ledgercore/domain/Currency.h"
#include "ledgercore/domain/Money.h"

namespace ledgercore::cli {

// The five, and only five, text-to-domain-type translations the CLI owns
// (see the Phase 11 design). Each throws CliUsageError for a syntax
// problem InputParsing itself detects; where the target domain type
// already validates its own shape (AccountCode, Currency), that type's
// constructor is called directly and its own ledgercore::LedgerException
// is left to propagate unchanged -- InputParsing never re-checks what the
// domain type already checks.

// "asset" | "liability" | "equity" | "revenue" | "expense", case
// insensitive. Throws CliUsageError if text does not match one of these --
// AccountType has no domain-level string parser of its own to defer to.
domain::AccountType parseAccountType(const std::string& text);

// Passes straight through to domain::AccountCode's own constructor, which
// throws domain::InvalidAccountException for an empty code.
domain::AccountCode parseAccountCode(const std::string& text);

// Passes straight through to domain::Currency's own constructor, which
// throws domain::InvalidCurrencyException for a malformed ISO-4217 code.
domain::Currency parseCurrency(const std::string& text);

// A decimal amount such as "250.00" or "-0.10". Splits the text into a
// major-units integer and a minor-units integer (1 fractional digit is
// treated as tenths, 2 as hundredths) and calls
// domain::Money::fromMajorUnits() -- per that factory's own documented
// design, decimal-string parsing was deliberately left for "whichever
// future module (Formula Engine, CLI) actually needs it." Throws
// CliUsageError for a structurally malformed string (e.g. "12.3.4",
// "abc", more than two fractional digits); domain::Money::fromMajorUnits()
// itself may still throw domain::InvalidMoneyException or
// domain::MoneyOverflowException for a structurally valid but
// out-of-range amount, and that exception is left to propagate unchanged.
domain::Money parseAmount(const std::string& text, const domain::Currency& currency);

// "YYYY-MM-DD", interpreted as UTC midnight. Throws CliUsageError for any
// shape that doesn't match (wrong length, non-digit characters, wrong
// separators, month outside 01-12, day outside 01-31). JournalEntry::date()
// is a bare std::chrono::system_clock::time_point with no domain-level
// string parser to defer to, by that type's own documented design.
std::chrono::system_clock::time_point parseDate(const std::string& text);

} // namespace ledgercore::cli
