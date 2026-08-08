#pragma once

#include <cstdint>
#include <string>

#include "ledgercore/domain/Currency.h"

namespace ledgercore::domain {

// An exact monetary amount: a signed count of minor units (e.g. cents)
// paired with a Currency. Immutable and freely copyable -- ordinary
// value-type semantics, unlike Account, which has identity and is
// deliberately non-copyable.
//
// Design decision (Phase 2, Step 1): Money is backed by std::int64_t,
// never double/float. Binary floating point cannot exactly represent
// most decimal fractions (0.1 + 0.2 != 0.3), which is disqualifying for
// a system whose core invariant is that debits exactly equal credits.
// Fixed-point integer arithmetic is exact by construction, not by
// discipline.
//
// There is no artificial maximum amount: any value representable by
// std::int64_t is valid. Every arithmetic operation checks for overflow
// *before* performing it and throws MoneyOverflowException rather than
// invoking undefined signed-integer-overflow behavior.
//
// Money deliberately does not support multiplication, division,
// rounding, decimal-string parsing, or currency conversion. Each of
// those requires information (a coefficient type, a rounding
// convention, an exchange rate) that isn't known yet at this phase;
// adding them now would mean guessing their shape. They belong to
// whichever future module (Formula Engine, CLI) actually needs them.
class Money {
public:
    static constexpr std::int64_t kMinorUnitsPerMajorUnit = 100;

    static Money ofMinorUnits(std::int64_t minorUnits, Currency currency);

    // majorUnits may be negative, zero, or positive. minorUnitsPart must
    // satisfy -99 <= minorUnitsPart <= 99, and its sign must not conflict
    // with majorUnits's sign (e.g. majorUnits=-5, minorUnitsPart=+10 is
    // rejected as ambiguous: is that -$5.10 or -$4.90?). When majorUnits
    // is 0, minorUnitsPart alone carries the sign -- the only way to
    // construct an amount between -$1.00 and $1.00 (exclusive) that is
    // negative, e.g. fromMajorUnits(0, -10, usd) == -$0.10.
    static Money fromMajorUnits(std::int64_t majorUnits, std::int64_t minorUnitsPart, Currency currency);

    static Money zero(Currency currency);

    std::int64_t minorUnits() const noexcept { return minorUnits_; }
    const Currency& currency() const noexcept { return currency_; }

    // Deterministic, locale-independent: "100.10 USD", "-100.10 USD", "0.00 USD".
    std::string toString() const;

    bool isZero() const noexcept { return minorUnits_ == 0; }
    bool isPositive() const noexcept { return minorUnits_ > 0; }
    bool isNegative() const noexcept { return minorUnits_ < 0; }

private:
    Money(std::int64_t minorUnits, Currency currency);

    std::int64_t minorUnits_;
    Currency currency_;
};

// Same-currency only: throws CurrencyMismatchException otherwise.
// Throws MoneyOverflowException if the result overflows std::int64_t.
Money operator+(const Money& lhs, const Money& rhs);
Money operator-(const Money& lhs, const Money& rhs);

// Throws MoneyOverflowException when negating the minimum representable
// value, which has no representable positive counterpart.
Money operator-(const Money& value);

// Well-defined even across currencies: a different currency simply
// means "not equal." Never throws.
bool operator==(const Money& lhs, const Money& rhs) noexcept;
bool operator!=(const Money& lhs, const Money& rhs) noexcept;

// Ordering across currencies has no well-defined answer (no conversion
// rate exists), so these throw CurrencyMismatchException in that case.
bool operator<(const Money& lhs, const Money& rhs);
bool operator<=(const Money& lhs, const Money& rhs);
bool operator>(const Money& lhs, const Money& rhs);
bool operator>=(const Money& lhs, const Money& rhs);

} // namespace ledgercore::domain
