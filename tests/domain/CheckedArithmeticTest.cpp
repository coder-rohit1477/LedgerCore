// The single checked-arithmetic implementation shared by Money, Rational,
// and the formula Evaluator (Phase 20). Boundary values -- especially
// INT64_MIN, where Phase 14 found undefined behavior -- are pinned here,
// at compile time where possible.

#include <gtest/gtest.h>

#include <cstdint>

#include "ledgercore/domain/CheckedArithmetic.h"

using ledgercore::domain::checked::kInt64Max;
using ledgercore::domain::checked::kInt64Min;
using ledgercore::domain::checked::magnitudeOf;
using ledgercore::domain::checked::wouldAddOverflow;
using ledgercore::domain::checked::wouldMultiplyOverflow;
using ledgercore::domain::checked::wouldSubtractOverflow;

static_assert(magnitudeOf(0) == 0u);
static_assert(magnitudeOf(-1) == 1u);
static_assert(magnitudeOf(kInt64Max) == 9223372036854775807u);
static_assert(magnitudeOf(kInt64Min) == 9223372036854775808u);

static_assert(!wouldAddOverflow(kInt64Max, 0));
static_assert(wouldAddOverflow(kInt64Max, 1));
static_assert(!wouldAddOverflow(kInt64Min, 0));
static_assert(wouldAddOverflow(kInt64Min, -1));
static_assert(!wouldAddOverflow(kInt64Max, kInt64Min));
static_assert(!wouldAddOverflow(-1, kInt64Min + 1));

static_assert(!wouldSubtractOverflow(kInt64Min, 0));
static_assert(wouldSubtractOverflow(kInt64Min, 1));
static_assert(wouldSubtractOverflow(0, kInt64Min));  // 0 - MIN == 2^63
static_assert(!wouldSubtractOverflow(-1, kInt64Min));
static_assert(wouldSubtractOverflow(kInt64Max, -1));

static_assert(!wouldMultiplyOverflow(0, kInt64Min));
static_assert(!wouldMultiplyOverflow(kInt64Min, 1));
static_assert(wouldMultiplyOverflow(kInt64Min, -1));
static_assert(wouldMultiplyOverflow(-1, kInt64Min));
static_assert(!wouldMultiplyOverflow(kInt64Max, -1));
static_assert(wouldMultiplyOverflow(kInt64Max, 2));
static_assert(!wouldMultiplyOverflow(3037000499, 3037000499));  // floor(sqrt(MAX))^2
static_assert(wouldMultiplyOverflow(3037000500, 3037000500));
// Documented conservatism: (MIN / 2) * 2 == MIN is representable but reported.
static_assert(wouldMultiplyOverflow(kInt64Min / 2, 2));

TEST(CheckedArithmeticTest, BoundaryResultsMatchAtRuntime) {
    // The same checks evaluated at runtime (not just in constant
    // expressions), so UBSan sees every path in the sanitizer build.
    volatile std::int64_t max = kInt64Max;
    volatile std::int64_t min = kInt64Min;
    EXPECT_TRUE(wouldAddOverflow(max, 1));
    EXPECT_TRUE(wouldAddOverflow(min, -1));
    EXPECT_TRUE(wouldSubtractOverflow(min, 1));
    EXPECT_TRUE(wouldSubtractOverflow(0, min));
    EXPECT_TRUE(wouldMultiplyOverflow(min, -1));
    EXPECT_FALSE(wouldMultiplyOverflow(min, 1));
    EXPECT_EQ(magnitudeOf(min), 9223372036854775808u);
    EXPECT_EQ(magnitudeOf(max), 9223372036854775807u);
}

TEST(CheckedArithmeticTest, NoFalsePositivesAcrossSmallValues) {
    for (std::int64_t a = -50; a <= 50; ++a) {
        for (std::int64_t b = -50; b <= 50; ++b) {
            EXPECT_FALSE(wouldAddOverflow(a, b));
            EXPECT_FALSE(wouldSubtractOverflow(a, b));
            EXPECT_FALSE(wouldMultiplyOverflow(a, b));
        }
    }
}
