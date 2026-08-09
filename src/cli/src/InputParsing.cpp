#include "InputParsing.h"

#include <cctype>
#include <exception>

#include "CommandParser.h"

namespace ledgercore::cli {

namespace {

[[noreturn]] void rejectAmount(const std::string& text) {
    throw CliUsageError("malformed amount: '" + text + "' (expected e.g. 250.00 or -0.10)");
}

[[noreturn]] void rejectDate(const std::string& text) {
    throw CliUsageError("malformed date: '" + text + "' (expected YYYY-MM-DD)");
}

std::int64_t parseDigitsOrRejectAmount(const std::string& digits, const std::string& original) {
    if (digits.empty()) {
        rejectAmount(original);
    }
    for (char c : digits) {
        if (c < '0' || c > '9') {
            rejectAmount(original);
        }
    }
    try {
        return std::stoll(digits);
    } catch (const std::exception&) {
        rejectAmount(original);
    }
}

// Days from the civil (proleptic Gregorian) calendar date to the Unix
// epoch (1970-01-01), via Howard Hinnant's well-known days_from_civil
// algorithm, translated to signed 64-bit arithmetic throughout so it
// compiles cleanly under -Wconversion/-Wsign-conversion without mixing
// signed and unsigned operands.
std::int64_t daysFromCivil(std::int64_t y, std::int64_t m, std::int64_t d) noexcept {
    y -= (m <= 2) ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;                        // [0, 399]
    const std::int64_t mp = (m > 2) ? (m - 3) : (m + 9);            // [0, 11]
    const std::int64_t doy = (153 * mp + 2) / 5 + d - 1;            // [0, 365]
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; // [0, 146096]
    return era * 146097 + doe - 719468;
}

} // namespace

domain::AccountType parseAccountType(const std::string& text) {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (lower == "asset") {
        return domain::AccountType::Asset;
    }
    if (lower == "liability") {
        return domain::AccountType::Liability;
    }
    if (lower == "equity") {
        return domain::AccountType::Equity;
    }
    if (lower == "revenue") {
        return domain::AccountType::Revenue;
    }
    if (lower == "expense") {
        return domain::AccountType::Expense;
    }
    throw CliUsageError("unknown account type: '" + text + "' (expected asset|liability|equity|revenue|expense)");
}

domain::AccountCode parseAccountCode(const std::string& text) {
    return domain::AccountCode(text);
}

domain::Currency parseCurrency(const std::string& text) {
    return domain::Currency(text);
}

domain::Money parseAmount(const std::string& text, const domain::Currency& currency) {
    if (text.empty()) {
        rejectAmount(text);
    }

    std::size_t index = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        index = 1;
    }
    if (index >= text.size()) {
        rejectAmount(text);
    }

    const std::size_t dot = text.find('.', index);
    std::string integerPart;
    std::string fractionalPart;
    if (dot == std::string::npos) {
        integerPart = text.substr(index);
    } else {
        integerPart = text.substr(index, dot - index);
        fractionalPart = text.substr(dot + 1);
        if (fractionalPart.find('.') != std::string::npos) {
            rejectAmount(text);
        }
        if (fractionalPart.empty() || fractionalPart.size() > 2) {
            rejectAmount(text);
        }
    }

    const std::int64_t majorMagnitude = parseDigitsOrRejectAmount(integerPart, text);
    std::int64_t minorMagnitude = 0;
    if (!fractionalPart.empty()) {
        minorMagnitude = parseDigitsOrRejectAmount(fractionalPart, text);
        if (fractionalPart.size() == 1) {
            minorMagnitude *= 10;
        }
    }

    const std::int64_t majorUnits = negative ? -majorMagnitude : majorMagnitude;
    const std::int64_t minorUnitsPart = negative ? -minorMagnitude : minorMagnitude;

    return domain::Money::fromMajorUnits(majorUnits, minorUnitsPart, currency);
}

std::chrono::system_clock::time_point parseDate(const std::string& text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-') {
        rejectDate(text);
    }
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (text[i] < '0' || text[i] > '9') {
            rejectDate(text);
        }
    }

    std::int64_t year = 0;
    std::int64_t month = 0;
    std::int64_t day = 0;
    try {
        year = std::stoll(text.substr(0, 4));
        month = std::stoll(text.substr(5, 2));
        day = std::stoll(text.substr(8, 2));
    } catch (const std::exception&) {
        rejectDate(text);
    }

    if (month < 1 || month > 12 || day < 1 || day > 31) {
        rejectDate(text);
    }

    const std::int64_t days = daysFromCivil(year, month, day);
    using DaysDuration = std::chrono::duration<std::int64_t, std::ratio<86400>>;
    return std::chrono::system_clock::time_point{}
           + std::chrono::duration_cast<std::chrono::system_clock::duration>(DaysDuration(days));
}

} // namespace ledgercore::cli
