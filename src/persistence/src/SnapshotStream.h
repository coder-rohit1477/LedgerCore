#pragma once

#include <istream>
#include <ostream>

#include "ledgercore/computed/ComputedAccountRegistry.h"
#include "ledgercore/domain/ChartOfAccounts.h"
#include "ledgercore/ledger/Ledger.h"
#include "ledgercore/persistence/SessionStore.h"

namespace ledgercore::persistence::detail {

// The snapshot format over streams. save() and load() (SessionStore.h)
// add only the file handling -- opening the file, and the temporary file
// plus atomic rename -- around these two, which carry every format rule
// and the same exception contract. Internal to persistence (not
// installed); exposed here so the fuzz targets can exercise the format
// entirely in memory.
void writeSnapshot(std::ostream& out, const domain::ChartOfAccounts& chart, const ledger::Ledger& ledger,
                   const computed::ComputedAccountRegistry& computedAccounts);

LoadedSession readSnapshot(std::istream& in);

} // namespace ledgercore::persistence::detail
