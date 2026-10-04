# Module libraries, in dependency order, then the CLI executable.
#
# Each module library is packaging-ready: its source include directory is
# wrapped in $<BUILD_INTERFACE:...> (the install tree supplies its own; see
# cmake/LedgerCorePackage.cmake), and its warning flags are PRIVATE compile
# options (ledgercore_target_warnings), so neither appears in the exported
# targets.

add_subdirectory(domain)
add_subdirectory(ledger)
add_subdirectory(posting)
add_subdirectory(trialbalance)
add_subdirectory(formula)
add_subdirectory(computed)
add_subdirectory(reporting)
add_subdirectory(closing)
add_subdirectory(journalquery)
add_subdirectory(persistence)

# The whole engine as one link target: LedgerCore::ledgercore, the same name
# in-tree (add_subdirectory()/FetchContent) and when installed
# (find_package(LedgerCore)). It carries no code of its own; it links every
# module library, so consumers get all public headers, the C++17
# requirement, and the libraries in a valid link order. Individual
# ledgercore_<module> targets remain available in-tree for finer-grained use.
set(LEDGERCORE_MODULE_LIBRARIES
    ledgercore_domain
    ledgercore_ledger
    ledgercore_posting
    ledgercore_trialbalance
    ledgercore_formula
    ledgercore_computed
    ledgercore_reporting
    ledgercore_closing
    ledgercore_journalquery
    ledgercore_persistence
    PARENT_SCOPE
)
# Debug libraries get a "d" suffix (ledgercore_domaind.lib / libledgercore_domaind.a),
# so a Debug and a Release build can be installed into the same prefix:
# with MSVC the two cannot be mixed in one link, and without distinct names
# the second install would overwrite the first.
set_target_properties(
    ledgercore_domain ledgercore_ledger ledgercore_posting ledgercore_trialbalance ledgercore_formula
    ledgercore_computed ledgercore_reporting ledgercore_closing ledgercore_journalquery ledgercore_persistence
    PROPERTIES DEBUG_POSTFIX "d")

add_library(ledgercore INTERFACE)
target_link_libraries(ledgercore
    INTERFACE
        ledgercore_persistence
        ledgercore_reporting
        ledgercore_closing
        ledgercore_journalquery
)
add_library(LedgerCore::ledgercore ALIAS ledgercore)

add_subdirectory(cli)
