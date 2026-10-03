#include "ledgercore/domain/JournalEntry.h"

#include <chrono>
#include <ratio>
#include <utility>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

static_assert(std::ratio_less_equal<std::chrono::system_clock::period, std::micro>::value,
              "kClosingCutoffOffset (1us) must be exactly representable by system_clock");

JournalEntry::JournalEntry(std::chrono::system_clock::time_point date,
                            std::string description,
                            std::vector<JournalEntryLine> lines,
                            Currency currency,
                            Money totalDebits,
                            Money totalCredits,
                            JournalEntryKind kind)
    : date_(date),
      description_(std::move(description)),
      lines_(std::move(lines)),
      currency_(std::move(currency)),
      totalDebits_(std::move(totalDebits)),
      totalCredits_(std::move(totalCredits)),
      kind_(kind) {}

JournalEntry JournalEntry::create(std::chrono::system_clock::time_point date,
                                   std::string description,
                                   std::vector<JournalEntryLine> lines) {
    return createValidated(date, std::move(description), std::move(lines), JournalEntryKind::Standard);
}

JournalEntry JournalEntry::createClosing(std::chrono::system_clock::time_point date,
                                          std::string description,
                                          std::vector<JournalEntryLine> lines) {
    if (date > std::chrono::system_clock::time_point::max() - kClosingCutoffOffset) {
        throw InvalidJournalEntryException("Closing entry date is too late for its cutoff to be representable");
    }
    return createValidated(date, std::move(description), std::move(lines), JournalEntryKind::Closing);
}

std::chrono::system_clock::time_point JournalEntry::closingCutoff() const {
    if (!isClosing()) {
        throw InvalidJournalEntryException("Only a closing entry has a closing cutoff");
    }
    return date_ + kClosingCutoffOffset;
}

JournalEntry JournalEntry::createValidated(std::chrono::system_clock::time_point date,
                                            std::string description,
                                            std::vector<JournalEntryLine> lines,
                                            JournalEntryKind kind) {
    if (lines.size() < 2) {
        throw InvalidJournalEntryException("JournalEntry requires at least two lines");
    }

    if (description.empty()) {
        throw InvalidJournalEntryException("JournalEntry description must not be empty");
    }

    const Currency currency = lines.front().amount().currency();
    for (const JournalEntryLine& line : lines) {
        if (line.amount().currency() != currency) {
            throw CurrencyMismatchException("All JournalEntryLine amounts must share one currency");
        }
    }

    Money totalDebits = Money::zero(currency);
    Money totalCredits = Money::zero(currency);
    for (const JournalEntryLine& line : lines) {
        if (line.isDebit()) {
            totalDebits = totalDebits + line.amount();
        } else {
            totalCredits = totalCredits + line.amount();
        }
    }

    if (totalDebits != totalCredits) {
        throw UnbalancedJournalEntryException(
            "JournalEntry does not balance: total debits " + totalDebits.toString() + " vs total credits "
            + totalCredits.toString());
    }

    return JournalEntry(date, std::move(description), std::move(lines), currency, totalDebits, totalCredits, kind);
}

} // namespace ledgercore::domain
