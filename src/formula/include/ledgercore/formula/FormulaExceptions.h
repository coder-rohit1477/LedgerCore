#pragma once

#include <cstddef>
#include <string>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::formula {

// A lexical or parse error: the formula text itself could not be
// tokenized or does not match the grammar. Lexical and parse errors are
// merged into one category rather than split across two, since both are
// "the formula text is malformed" from a caller's perspective, and both
// carry the same useful piece of context: a character offset into the
// source string. Formulas are single-line strings, so an offset alone is
// sufficient -- no line/column tracking is needed.
class FormulaSyntaxException : public ledgercore::LedgerException {
public:
    FormulaSyntaxException(const std::string& message, std::size_t offset)
        : LedgerException(message + " (at offset " + std::to_string(offset) + ")"), offset_(offset) {}

    std::size_t offset() const noexcept { return offset_; }

private:
    std::size_t offset_;
};

// A formula referenced an AccountCode the supplied AccountResolver does
// not recognize. Split out from FormulaEvaluationException because it is
// the one evaluation-time failure a caller plausibly wants to handle
// differently (e.g. "account not created yet -- show blank" vs. "the
// formula itself is broken"), mirroring the existing
// posting::AccountNotFoundException precedent for "the resolver couldn't
// find this reference."
class UnknownAccountReferenceException : public ledgercore::LedgerException {
public:
    explicit UnknownAccountReferenceException(const std::string& message) : LedgerException(message) {}
};

// Any other evaluation-time failure: division by zero, Money*Money or
// Money/Money attempted, dimensional mismatch (Money +/- a dimensionless
// scalar), Rational overflow, or overflow while scaling a Money value by
// a Rational.
//
// Money+Money, Money-Money, and unary minus on Money are deliberately
// NOT covered here: those reuse domain::CurrencyMismatchException and
// domain::MoneyOverflowException directly, since Money's own operators
// already enforce those rules correctly and there is nothing to add by
// wrapping them.
class FormulaEvaluationException : public ledgercore::LedgerException {
public:
    explicit FormulaEvaluationException(const std::string& message) : LedgerException(message) {}
};

} // namespace ledgercore::formula
