#pragma once

#include <utility>
#include <variant>

#include "ledgercore/domain/Money.h"
#include "ledgercore/formula/AccountResolver.h"
#include "ledgercore/formula/Ast.h"
#include "ledgercore/formula/FormulaExceptions.h"
#include "ledgercore/formula/Rational.h"

namespace ledgercore::formula {

// The result of evaluating a formula: either a currency-bound Money
// value (whenever an account reference entered the computation) or a
// dimensionless Rational (a formula built entirely from literals). The
// Evaluator never invents a currency for a pure-scalar result -- that
// choice, if one is ever needed, belongs to the caller.
class FormulaValue {
public:
    explicit FormulaValue(domain::Money value) : value_(std::move(value)) {}
    explicit FormulaValue(Rational value) : value_(std::move(value)) {}

    bool isMoney() const noexcept { return std::holds_alternative<domain::Money>(value_); }
    bool isScalar() const noexcept { return std::holds_alternative<Rational>(value_); }

    const domain::Money& asMoney() const {
        if (!isMoney()) {
            throw FormulaEvaluationException("FormulaValue does not hold a Money result");
        }
        return std::get<domain::Money>(value_);
    }

    const Rational& asScalar() const {
        if (!isScalar()) {
            throw FormulaEvaluationException("FormulaValue does not hold a scalar result");
        }
        return std::get<Rational>(value_);
    }

private:
    std::variant<domain::Money, Rational> value_;
};

// Evaluates an AST against a caller-supplied resolver. Deterministic and
// side-effect free: the same root plus a resolver returning the same
// values always produces the same result.
//
// Operator rules (see Rational.h/domain/Money.h for the arithmetic
// itself):
//   Money +/- Money   requires the same currency; reuses domain::Money's
//                      own operators, so domain::CurrencyMismatchException
//                      and domain::MoneyOverflowException apply unchanged.
//   Rational +/- Rational  always exact.
//   Money +/- Rational      rejected (dimensional mismatch).
//   Money * Rational, Rational * Money, Money / Rational
//                      scales the Money amount, rounding half away from
//                      zero only when the exact result is not a whole
//                      number of minor units.
//   Rational * Rational, Rational / Rational   always exact.
//   Money * Money, Money / Money, Rational / Money   rejected.
// All rejections and overflow/division-by-zero failures throw
// FormulaEvaluationException. Unknown account references throw
// UnknownAccountReferenceException.
FormulaValue evaluate(const AstNode& root, const AccountResolver& resolver);

} // namespace ledgercore::formula
