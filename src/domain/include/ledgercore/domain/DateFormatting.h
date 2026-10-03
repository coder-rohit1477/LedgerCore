#pragma once

#include <chrono>
#include <string>

namespace ledgercore::domain {

// Deterministic, locale-independent UTC rendering of a business date, for
// messages and listings: "2027-01-01" for an instant at UTC midnight
// (every date the CLI accepts), otherwise "2027-12-31T23:59:59Z", with a
// ".nnnnnnnnn" nanosecond fraction when the instant is not on a whole
// second. Safe for every representable time_point. Formatting only --
// LedgerCore has no domain-level date parser (the CLI owns parsing).
std::string formatUtc(std::chrono::system_clock::time_point instant);

} // namespace ledgercore::domain
