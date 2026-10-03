#include "ledgercore/domain/DateFormatting.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace ledgercore::domain {

namespace {

// Howard Hinnant's civil_from_days: days since 1970-01-01 to a proleptic
// Gregorian (year, month, day), in signed 64-bit arithmetic.
void civilFromDays(std::int64_t z, std::int64_t& year, std::int64_t& month, std::int64_t& day) noexcept {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;                                 // [0, 146096]
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);          // [0, 365]
    const std::int64_t mp = (5 * doy + 2) / 153;                               // [0, 11]
    day = doy - (153 * mp + 2) / 5 + 1;
    month = mp < 10 ? mp + 3 : mp - 9;
    year = yoe + era * 400 + (month <= 2 ? 1 : 0);
}

} // namespace

std::string formatUtc(std::chrono::system_clock::time_point instant) {
    using Clock = std::chrono::system_clock;
    constexpr std::int64_t kTicksPerDay =
        static_cast<std::int64_t>(86400) * Clock::period::den / Clock::period::num;

    // Floor division of the raw tick count into whole days plus a
    // non-negative remainder; truncating division then a single
    // correction step, so no intermediate value can overflow.
    const std::int64_t ticks = instant.time_since_epoch().count();
    std::int64_t days = ticks / kTicksPerDay;
    std::int64_t remainder = ticks % kTicksPerDay;
    if (remainder < 0) {
        days -= 1;
        remainder += kTicksPerDay;
    }

    std::int64_t year = 0;
    std::int64_t month = 0;
    std::int64_t day = 0;
    civilFromDays(days, year, month, day);

    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << year << '-' << std::setw(2) << month << '-' << std::setw(2) << day;
    if (remainder == 0) {
        return out.str();
    }

    // remainder is less than one day, so converting it to nanoseconds
    // cannot overflow.
    const std::int64_t nanosOfDay =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::duration(remainder)).count();
    const std::int64_t secondsOfDay = nanosOfDay / 1000000000;
    const std::int64_t fraction = nanosOfDay % 1000000000;
    out << 'T' << std::setw(2) << secondsOfDay / 3600 << ':' << std::setw(2) << (secondsOfDay / 60) % 60 << ':'
        << std::setw(2) << secondsOfDay % 60;
    if (fraction != 0) {
        out << '.' << std::setw(9) << fraction;
    }
    out << 'Z';
    return out.str();
}

} // namespace ledgercore::domain
