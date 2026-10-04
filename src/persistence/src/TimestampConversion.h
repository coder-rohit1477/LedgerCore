#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <ratio>

namespace ledgercore::persistence::detail {

// Converts a persisted nanosecond count to Duration only when Duration
// represents it exactly; std::nullopt otherwise. Snapshots store
// nanoseconds, but system_clock's tick is coarser on some platforms
// (microseconds on macOS, 100ns with MSVC), and a plain duration_cast would
// silently truncate toward zero -- moving a pre-1970 instant *later*, e.g.
// across a day or period boundary. The check is an exact round trip on the
// signed value, so it treats negative and positive counts identically.
//
// Internal to persistence (not installed); a template only so tests can
// exercise every tick size on any host.
//
// Precondition: the caller has range-checked nanos, so converting to
// Duration cannot overflow.
template <typename Duration>
std::optional<Duration> exactDurationFromNanos(std::int64_t nanos) {
    static_assert(std::ratio_greater_equal<typename Duration::period, std::nano>::value,
                  "Duration must not be finer than the snapshot's nanoseconds");
    const std::chrono::nanoseconds persisted(nanos);
    const Duration converted = std::chrono::duration_cast<Duration>(persisted);
    if (std::chrono::duration_cast<std::chrono::nanoseconds>(converted) != persisted) {
        return std::nullopt;
    }
    return converted;
}

} // namespace ledgercore::persistence::detail
