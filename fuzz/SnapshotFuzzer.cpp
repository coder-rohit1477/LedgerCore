// libFuzzer target: arbitrary bytes as a LedgerCore snapshot, read entirely
// in memory (no files are created).
//
// Contract checked for every input:
//   - reading either throws one of load()'s documented exceptions
//     (persistence::PersistenceException or a ledgercore::LedgerException)
//     or yields a session; anything else escaping is reported as a crash;
//   - the outcome is deterministic (a second read gives the same result);
//   - an accepted snapshot round-trips: what save() writes for it reads
//     back and is written again byte-for-byte identically.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "ledgercore/domain/DomainExceptions.h"
#include "ledgercore/persistence/PersistenceExceptions.h"

#include "SnapshotStream.h"

namespace {

struct Outcome {
    bool loaded = false;
    std::string text;  // the re-saved snapshot, or the error message

    bool operator==(const Outcome& other) const { return loaded == other.loaded && text == other.text; }
};

Outcome readAndWrite(const std::string& input) {
    namespace persistence = ledgercore::persistence;
    std::istringstream in(input);
    try {
        const persistence::LoadedSession session = persistence::detail::readSnapshot(in);
        std::ostringstream out;
        persistence::detail::writeSnapshot(out, *session.chart, *session.ledger, *session.computedAccounts);
        return Outcome{true, out.str()};
    } catch (const persistence::PersistenceException& e) {
        return Outcome{false, std::string("persistence: ") + e.what()};
    } catch (const ledgercore::LedgerException& e) {
        return Outcome{false, std::string("ledger: ") + e.what()};
    }
}

[[noreturn]] void fail(const char* what) {
    std::fprintf(stderr, "snapshot fuzzer: %s\n", what);
    std::abort();
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string input(reinterpret_cast<const char*>(data), size);

    const Outcome first = readAndWrite(input);
    if (!(readAndWrite(input) == first)) {
        fail("reading the same input twice gave different outcomes");
    }
    if (first.loaded) {
        const Outcome reread = readAndWrite(first.text);
        if (!reread.loaded) {
            fail("a snapshot written by save() could not be read back");
        }
        if (reread.text != first.text) {
            fail("save -> load -> save is not byte-identical");
        }
    }
    return 0;
}
