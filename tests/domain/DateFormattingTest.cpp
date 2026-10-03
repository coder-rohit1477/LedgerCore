#include <gtest/gtest.h>

#include <chrono>

#include "ledgercore/domain/DateFormatting.h"

using ledgercore::domain::formatUtc;
using TimePoint = std::chrono::system_clock::time_point;

namespace {

TimePoint epochSeconds(long long seconds) {
    return TimePoint{} + std::chrono::seconds(seconds);
}

} // namespace

TEST(DateFormattingTest, MidnightIsRenderedAsDateOnly) {
    EXPECT_EQ(formatUtc(TimePoint{}), "1970-01-01");
    EXPECT_EQ(formatUtc(epochSeconds(1798761600)), "2027-01-01");
    EXPECT_EQ(formatUtc(epochSeconds(951782400)), "2000-02-29");
}

TEST(DateFormattingTest, PreEpochDatesUseFloorDivision) {
    EXPECT_EQ(formatUtc(epochSeconds(-2208988800)), "1900-01-01");
    EXPECT_EQ(formatUtc(epochSeconds(-1)), "1969-12-31T23:59:59Z");
}

TEST(DateFormattingTest, TimeOfDayAndSubSecondFractionAreShown) {
    EXPECT_EQ(formatUtc(epochSeconds(1798761600) - std::chrono::seconds(1)), "2026-12-31T23:59:59Z");
    EXPECT_EQ(formatUtc(epochSeconds(1798761600) - std::chrono::microseconds(1)),
              "2026-12-31T23:59:59.999999000Z");
}

TEST(DateFormattingTest, ExtremeInstantsDoNotOverflow) {
    EXPECT_FALSE(formatUtc(std::chrono::system_clock::time_point::min()).empty());
    EXPECT_FALSE(formatUtc(std::chrono::system_clock::time_point::max()).empty());
}
