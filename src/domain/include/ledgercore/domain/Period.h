#pragma once

#include <chrono>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

// An immutable, half-open calendar-timestamp range: [start, end). Chosen
// specifically so adjacent periods tile a timeline without ambiguity or
// double-counting at a shared boundary instant -- e.g. April's end() is
// exactly May's start(), and that shared instant belongs to May, not
// April, with no time-of-day hack required.
//
// Backed directly by std::chrono::system_clock::time_point, the same
// type JournalEntry::date() already uses -- there is no separate Date
// type, and Period does not know about fiscal calendars, quarters, or
// any calendar concept beyond a raw instant range.
class Period {
public:
    // Throws InvalidPeriodException if start >= end: a period must span
    // a genuinely non-empty, forward-moving range. This is distinct from
    // a validly constructed period that simply has zero matching
    // transactions at query time, which is not an error (see callers in
    // trialbalance).
    Period(std::chrono::system_clock::time_point start, std::chrono::system_clock::time_point end)
        : start_(start), end_(end) {
        if (start_ >= end_) {
            throw InvalidPeriodException("Period start must be strictly before end");
        }
    }

    std::chrono::system_clock::time_point start() const noexcept { return start_; }
    std::chrono::system_clock::time_point end() const noexcept { return end_; }

    // start() <= date && date < end().
    bool contains(std::chrono::system_clock::time_point date) const noexcept {
        return start_ <= date && date < end_;
    }

private:
    std::chrono::system_clock::time_point start_;
    std::chrono::system_clock::time_point end_;
};

} // namespace ledgercore::domain
