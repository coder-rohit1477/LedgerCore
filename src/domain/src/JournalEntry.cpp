#include "ledgercore/domain/JournalEntry.h"

#include <utility>

#include "ledgercore/domain/DomainExceptions.h"

namespace ledgercore::domain {

JournalEntry::JournalEntry(std::chrono::system_clock::time_point date,
                            std::string description,
                            std::vector<JournalEntryLine> lines,
                            Currency currency,
                            Money totalDebits,
                            Money totalCredits)
    : date_(date),
      description_(std::move(description)),
      lines_(std::move(lines)),
      currency_(std::move(currency)),
      totalDebits_(std::move(totalDebits)),
      totalCredits_(std::move(totalCredits)) {}

JournalEntry JournalEntry::create(std::chrono::system_clock::time_point date,
                                   std::string description,
                                   std::vector<JournalEntryLine> lines) {
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

    return JournalEntry(date, std::move(description), std::move(lines), currency, totalDebits, totalCredits);
}

} // namespace ledgercore::domain
