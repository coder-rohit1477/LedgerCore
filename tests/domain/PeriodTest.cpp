#include <gtest/gtest.h>

#include <chrono>

#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/domain/Period.h"

using ledgercore::domain::InvalidPeriodException;
using ledgercore::domain::Period;

namespace {
// A synthetic, deterministic "day N" timestamp -- no real calendar
// library is needed since Period only cares about time_point ordering.
std::chrono::system_clock::time_point day(int n) {
    return std::chrono::system_clock::time_point{} + std::chrono::hours(24 * n);
}
} // namespace

TEST(PeriodTest, ValidPeriodConstruction) {
    Period period(day(1), day(10));
    EXPECT_EQ(period.start(), day(1));
    EXPECT_EQ(period.end(), day(10));
}

TEST(PeriodTest, StartEqualsEndIsRejected) {
    EXPECT_THROW(Period(day(5), day(5)), InvalidPeriodException);
}

TEST(PeriodTest, ReversedPeriodIsRejected) {
    EXPECT_THROW(Period(day(10), day(1)), InvalidPeriodException);
}

TEST(PeriodTest, ContainsIncludesStartBoundary) {
    Period period(day(1), day(10));
    EXPECT_TRUE(period.contains(day(1)));
}

TEST(PeriodTest, ContainsExcludesEndBoundary) {
    Period period(day(1), day(10));
    EXPECT_FALSE(period.contains(day(10)));
}

TEST(PeriodTest, ContainsIncludesMidpoint) {
    Period period(day(1), day(10));
    EXPECT_TRUE(period.contains(day(5)));
}

TEST(PeriodTest, ContainsExcludesBeforeStart) {
    Period period(day(1), day(10));
    EXPECT_FALSE(period.contains(day(0)));
}

TEST(PeriodTest, ContainsExcludesAfterEnd) {
    Period period(day(1), day(10));
    EXPECT_FALSE(period.contains(day(11)));
}

TEST(PeriodTest, AdjacentPeriodsTileWithoutOverlapOrGap) {
    Period april(day(1), day(31));
    Period may(day(31), day(61));

    // The shared boundary instant belongs to exactly one period: May.
    EXPECT_FALSE(april.contains(day(31)));
    EXPECT_TRUE(may.contains(day(31)));
    EXPECT_FALSE(april.contains(day(31)) && may.contains(day(31)));

    // The instant just before the boundary still belongs to April only.
    EXPECT_TRUE(april.contains(day(30)));
    EXPECT_FALSE(may.contains(day(30)));
}
