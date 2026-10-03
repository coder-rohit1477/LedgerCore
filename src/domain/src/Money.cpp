#include "ledgercore/domain/Money.h"

#include <sstream>
#include <utility>

#include "ledgercore/domain/CheckedArithmetic.h"
#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

namespace {

using checked::wouldAddOverflow;
using checked::wouldSubtractOverflow;

using checked::kInt64Max;
using checked::kInt64Min;

} // namespace

Money::Money(std::int64_t minorUnits, Currency currency) : minorUnits_(minorUnits), currency_(std::move(currency)) {}

Money Money::ofMinorUnits(std::int64_t minorUnits, Currency currency) {
    return Money(minorUnits, std::move(currency));
}

Money Money::fromMajorUnits(std::int64_t majorUnits, std::int64_t minorUnitsPart, Currency currency) {
    if (minorUnitsPart <= -kMinorUnitsPerMajorUnit || minorUnitsPart >= kMinorUnitsPerMajorUnit) {
        throw InvalidMoneyException("minorUnitsPart must be in the range -99..99");
    }
    if (majorUnits > 0 && minorUnitsPart < 0) {
        throw InvalidMoneyException("minorUnitsPart cannot be negative when majorUnits is positive");
    }
    if (majorUnits < 0 && minorUnitsPart > 0) {
        throw InvalidMoneyException("minorUnitsPart cannot be positive when majorUnits is negative");
    }

    bool multiplicationOverflows = false;
    if (majorUnits > 0) {
        multiplicationOverflows = majorUnits > kInt64Max / kMinorUnitsPerMajorUnit;
    } else if (majorUnits < 0) {
        multiplicationOverflows = majorUnits < kInt64Min / kMinorUnitsPerMajorUnit;
    }
    if (multiplicationOverflows) {
        throw MoneyOverflowException("fromMajorUnits: majorUnits out of representable range");
    }

    const std::int64_t scaled = majorUnits * kMinorUnitsPerMajorUnit;
    if (wouldAddOverflow(scaled, minorUnitsPart)) {
        throw MoneyOverflowException("fromMajorUnits: amount out of representable range");
    }

    return Money(scaled + minorUnitsPart, std::move(currency));
}

Money Money::zero(Currency currency) {
    return Money(0, std::move(currency));
}

std::string Money::toString() const {
    std::int64_t major = minorUnits_ / kMinorUnitsPerMajorUnit;
    std::int64_t minor = minorUnits_ % kMinorUnitsPerMajorUnit;
    if (minor < 0) {
        minor = -minor;
    }
    if (major < 0) {
        major = -major;
    }

    std::ostringstream out;
    if (minorUnits_ < 0) {
        out << '-';
    }
    out << major << '.';
    if (minor < 10) {
        out << '0';
    }
    out << minor << ' ' << currency_.code();
    return out.str();
}

Money operator+(const Money& lhs, const Money& rhs) {
    if (lhs.currency() != rhs.currency()) {
        throw CurrencyMismatchException("Cannot add Money values of different currencies");
    }
    if (wouldAddOverflow(lhs.minorUnits(), rhs.minorUnits())) {
        throw MoneyOverflowException("Money addition overflows the representable range");
    }
    return Money::ofMinorUnits(lhs.minorUnits() + rhs.minorUnits(), lhs.currency());
}

Money operator-(const Money& lhs, const Money& rhs) {
    if (lhs.currency() != rhs.currency()) {
        throw CurrencyMismatchException("Cannot subtract Money values of different currencies");
    }
    if (wouldSubtractOverflow(lhs.minorUnits(), rhs.minorUnits())) {
        throw MoneyOverflowException("Money subtraction overflows the representable range");
    }
    return Money::ofMinorUnits(lhs.minorUnits() - rhs.minorUnits(), lhs.currency());
}

Money operator-(const Money& value) {
    if (value.minorUnits() == kInt64Min) {
        throw MoneyOverflowException("Cannot negate the minimum representable Money value");
    }
    return Money::ofMinorUnits(-value.minorUnits(), value.currency());
}

bool operator==(const Money& lhs, const Money& rhs) noexcept {
    return lhs.currency() == rhs.currency() && lhs.minorUnits() == rhs.minorUnits();
}

bool operator!=(const Money& lhs, const Money& rhs) noexcept {
    return !(lhs == rhs);
}

bool operator<(const Money& lhs, const Money& rhs) {
    if (lhs.currency() != rhs.currency()) {
        throw CurrencyMismatchException("Cannot compare Money values of different currencies");
    }
    return lhs.minorUnits() < rhs.minorUnits();
}

bool operator<=(const Money& lhs, const Money& rhs) {
    if (lhs.currency() != rhs.currency()) {
        throw CurrencyMismatchException("Cannot compare Money values of different currencies");
    }
    return lhs.minorUnits() <= rhs.minorUnits();
}

bool operator>(const Money& lhs, const Money& rhs) {
    return rhs < lhs;
}

bool operator>=(const Money& lhs, const Money& rhs) {
    return rhs <= lhs;
}

} // namespace ledgercore::domain
