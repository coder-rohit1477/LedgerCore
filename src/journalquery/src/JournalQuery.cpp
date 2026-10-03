#include "ledgercore/journalquery/JournalQuery.h"

#include "ledgercore/domain/JournalEntryLine.h"

namespace ledgercore::journalquery {

JournalQuery JournalQuery::withDateRange(const domain::Period& range) const {
    JournalQuery narrowed = *this;
    narrowed.dateRange_ = range;
    return narrowed;
}

JournalQuery JournalQuery::withAccount(domain::AccountId account) const {
    JournalQuery narrowed = *this;
    narrowed.account_ = account;
    return narrowed;
}

JournalQuery JournalQuery::withKind(EntryKindFilter kind) const {
    JournalQuery narrowed = *this;
    narrowed.kind_ = kind;
    return narrowed;
}

bool JournalQuery::matches(const domain::JournalEntry& entry) const {
    if (kind_ == EntryKindFilter::StandardOnly && entry.isClosing()) {
        return false;
    }
    if (kind_ == EntryKindFilter::ClosingOnly && !entry.isClosing()) {
        return false;
    }
    if (dateRange_.has_value() && !dateRange_->contains(entry.date())) {
        return false;
    }
    if (account_.has_value()) {
        bool involved = false;
        for (const domain::JournalEntryLine& line : entry.lines()) {
            if (line.accountId() == *account_) {
                involved = true;
                break;
            }
        }
        if (!involved) {
            return false;
        }
    }
    return true;
}

std::vector<ledger::PostedJournalEntry> findJournalEntries(const ledger::Ledger& ledger, const JournalQuery& query) {
    std::vector<ledger::PostedJournalEntry> results;
    for (const ledger::PostedJournalEntry& posted : ledger.postedEntries()) {
        if (query.matches(posted.entry())) {
            results.push_back(posted);
        }
    }
    return results;
}

} // namespace ledgercore::journalquery
