#pragma once

#include <stdexcept>
#include <string>

namespace ledgercore::persistence {

// Root of the persistence exception hierarchy: infrastructure and
// file-format failures (unreadable file, malformed record, unsupported
// version), never accounting-domain invariant failures. Deliberately NOT
// derived from ledgercore::LedgerException -- a corrupted snapshot file is
// not a business-rule violation the way an unbalanced JournalEntry is, and
// callers may reasonably want to handle the two categories differently.
// Every domain-rule violation discovered while replaying an otherwise
// well-formed record (duplicate account code, unbalanced entry, unknown
// account, invalid formula, ...) is left to propagate as the existing,
// unmodified ledgercore::LedgerException subclass it already is -- this
// hierarchy is never used for those.
class PersistenceException : public std::runtime_error {
public:
    explicit PersistenceException(const std::string& message) : std::runtime_error(message) {}
};

// The snapshot file's structure does not match the expected grammar:
// missing/malformed header, a record with the wrong field count, an
// unrecognized record or account-type token, a malformed integer field, an
// unterminated quoted string, or a file that ends mid-record.
class PersistenceFormatException : public PersistenceException {
public:
    explicit PersistenceFormatException(const std::string& message) : PersistenceException(message) {}
};

// The snapshot declares a format version this build does not support.
class PersistenceVersionException : public PersistenceException {
public:
    explicit PersistenceVersionException(const std::string& message) : PersistenceException(message) {}
};

} // namespace ledgercore::persistence
