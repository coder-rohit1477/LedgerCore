#pragma once

#include <cstdint>
#include <limits>

// Internal implementation detail shared by domain::Money and the formula
// engine's Rational/Evaluator: the single definition of LedgerCore's
// overflow pre-checks for std::int64_t. Not part of the supported public
// API -- callers outside LedgerCore's own arithmetic types should use
// Money/Rational, which already apply these checks.
//
// Every function answers "would this operation overflow?" *without*
// performing any operation that could itself overflow, so callers can
// throw before invoking undefined signed-overflow behavior.
namespace ledgercore::domain::checked {

inline constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
inline constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();

// |value| as an unsigned 64-bit integer, exact for every input including
// INT64_MIN (whose magnitude, 2^63, has no signed representation).
constexpr std::uint64_t magnitudeOf(std::int64_t value) noexcept {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return static_cast<std::uint64_t>(-(value + 1)) + 1;
}

// Whether a + b would overflow std::int64_t.
constexpr bool wouldAddOverflow(std::int64_t a, std::int64_t b) noexcept {
    if (b > 0) {
        return a > kInt64Max - b;
    }
    if (b < 0) {
        return a < kInt64Min - b;
    }
    return false;
}

// Whether a - b would overflow std::int64_t.
constexpr bool wouldSubtractOverflow(std::int64_t a, std::int64_t b) noexcept {
    if (b > 0) {
        return a < kInt64Min + b;
    }
    if (b < 0) {
        return a > kInt64Max + b;
    }
    return false;
}

// Whether a * b would overflow std::int64_t. Conservative at exactly
// INT64_MIN: only INT64_MIN * 1 (and 1 * INT64_MIN) is reported safe, so
// e.g. (INT64_MIN / 2) * 2 is reported as overflowing even though the
// product is representable. Callers that later negate a product (Rational
// sign normalization) must still guard INT64_MIN themselves.
constexpr bool wouldMultiplyOverflow(std::int64_t a, std::int64_t b) noexcept {
    if (a == 0 || b == 0) {
        return false;
    }
    if (a == kInt64Min || b == kInt64Min) {
        const std::int64_t other = (a == kInt64Min) ? b : a;
        return other != 1;
    }
    const std::int64_t absA = a < 0 ? -a : a;
    const std::int64_t absB = b < 0 ? -b : b;
    return absA > kInt64Max / absB;
}

} // namespace ledgercore::domain::checked
