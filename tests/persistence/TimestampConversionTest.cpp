// Exact conversion of persisted nanosecond timestamps
// (src/persistence/src/TimestampConversion.h), instantiated for every
// system_clock tick a supported platform uses -- so the rejection logic is
// exercised on every host, not only where the host clock is coarse. The
// expectation is computed independently of the implementation: a count is
// representable exactly when it is a whole multiple of the tick.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <ratio>
#include <vector>

#include "TimestampConversion.h"

namespace {

using ledgercore::persistence::detail::exactDurationFromNanos;

using Nanoseconds = std::chrono::nanoseconds;
using HundredNanoseconds = std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>;
using Microseconds = std::chrono::microseconds;
using Seconds = std::chrono::seconds;

constexpr std::int64_t kNanosPerDay = 86'400'000'000'000;
constexpr std::int64_t kMinSupportedNanos = -2'208'988'800'000'000'000;  // 1900-01-01T00:00:00Z
constexpr std::int64_t kEndOfSupportedNanos = 7'258'118'400'000'000'000; // 2200-01-01T00:00:00Z

// Epoch, both sides of it, both sides of every tick size, day boundaries
// (1970-01-02 and 1969-12-31), and the supported-range edges.
const std::vector<std::int64_t> kCases = {
    0,
    1, -1,
    99, -99, 100, -100, 101, -101,
    999, -999, 1'000, -1'000, 1'001, -1'001,
    999'999'999, -999'999'999, 1'000'000'000, -1'000'000'000,
    kNanosPerDay, kNanosPerDay - 1, kNanosPerDay + 1,
    -kNanosPerDay, -kNanosPerDay - 1, -kNanosPerDay + 1,
    kMinSupportedNanos, kMinSupportedNanos + 1, kMinSupportedNanos + 100, kMinSupportedNanos + 1'000,
    kEndOfSupportedNanos, kEndOfSupportedNanos - 1, kEndOfSupportedNanos - 100, kEndOfSupportedNanos - 1'000,
};

template <typename Duration>
void expectExactOrRejected() {
    const std::int64_t tickNanos = std::chrono::duration_cast<Nanoseconds>(Duration(1)).count();
    for (const std::int64_t nanos : kCases) {
        const std::optional<Duration> converted = exactDurationFromNanos<Duration>(nanos);
        if (nanos % tickNanos == 0) {
            ASSERT_TRUE(converted.has_value()) << nanos << " ns, tick " << tickNanos << " ns";
            EXPECT_EQ(converted->count(), nanos / tickNanos) << nanos;
            EXPECT_EQ(std::chrono::duration_cast<Nanoseconds>(*converted).count(), nanos) << nanos;
        } else {
            EXPECT_FALSE(converted.has_value()) << nanos << " ns, tick " << tickNanos << " ns";
        }
    }
}

} // namespace

TEST(TimestampConversionTest, NanosecondTicksRepresentEveryTimestamp) {
    expectExactOrRejected<Nanoseconds>();
    for (const std::int64_t nanos : kCases) {
        EXPECT_TRUE(exactDurationFromNanos<Nanoseconds>(nanos).has_value()) << nanos;
    }
}

TEST(TimestampConversionTest, HundredNanosecondTicksRejectWhatTheyCannotRepresent) {
    expectExactOrRejected<HundredNanoseconds>();
}

TEST(TimestampConversionTest, MicrosecondTicksRejectWhatTheyCannotRepresent) {
    expectExactOrRejected<Microseconds>();
}

TEST(TimestampConversionTest, CoarseTicksRejectWhatTheyCannotRepresent) {
    expectExactOrRejected<Seconds>();
}

// The Phase 24 defect: truncation toward zero moved 1969-12-31T23:59:59.999999999
// to exactly 1970-01-01T00:00:00 on a microsecond clock -- a different day.
// It must be rejected, never moved, and an exactly representable pre-1970
// instant must keep its (negative) value.
TEST(TimestampConversionTest, PreEpochTimestampsAreNeverMovedAcrossTheEpoch) {
    EXPECT_FALSE(exactDurationFromNanos<Microseconds>(-1).has_value());
    EXPECT_FALSE(exactDurationFromNanos<HundredNanoseconds>(-1).has_value());
    EXPECT_FALSE(exactDurationFromNanos<Microseconds>(-kNanosPerDay - 1).has_value());

    const std::optional<Microseconds> lastMicrosecondOf1969 = exactDurationFromNanos<Microseconds>(-1'000);
    ASSERT_TRUE(lastMicrosecondOf1969.has_value());
    EXPECT_EQ(lastMicrosecondOf1969->count(), -1);
    EXPECT_LT(*lastMicrosecondOf1969, Microseconds(0));
}

// Every supported platform's tick divides one microsecond, so every
// microsecond-aligned timestamp -- everything the CLI and the closing
// engine produce -- loads on all of them.
TEST(TimestampConversionTest, MicrosecondAlignedTimestampsLoadOnEverySupportedTick) {
    for (const std::int64_t nanos : kCases) {
        if (nanos % 1'000 != 0) {
            continue;
        }
        EXPECT_TRUE(exactDurationFromNanos<Nanoseconds>(nanos).has_value()) << nanos;
        EXPECT_TRUE(exactDurationFromNanos<HundredNanoseconds>(nanos).has_value()) << nanos;
        EXPECT_TRUE(exactDurationFromNanos<Microseconds>(nanos).has_value()) << nanos;
    }
}
