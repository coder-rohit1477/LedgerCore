add_executable(ledgercore_cli
    src/main.cpp
    src/LedgerSession.cpp
    src/CommandParser.cpp
    src/InputParsing.cpp
    src/OutputFormatting.cpp
)

set_target_properties(ledgercore_cli PROPERTIES OUTPUT_NAME ledgercore)

# domain, ledger, formula, and trialbalance are deliberately not listed
# here even though the CLI uses their types directly: each is already a
# PUBLIC dependency of at least one target below (posting -> domain+ledger;
# computed -> domain+ledger+formula; reporting -> domain+trialbalance), so
# CMake already propagates both their headers and their linked libraries
# transitively. Listing them again here only caused the linker to see each
# duplicated on the link line ("ignoring duplicate libraries"). posting,
# computed, and reporting remain direct links because nothing else in this
# list re-exports them.
target_link_libraries(ledgercore_cli
    PRIVATE
        ledgercore_posting
        ledgercore_computed
        ledgercore_reporting
        ledgercore_warnings
)
