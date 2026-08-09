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
# PUBLIC dependency of at least one target below (persistence ->
# domain+ledger+posting+computed; computed -> domain+ledger+formula;
# reporting -> domain+trialbalance), so CMake already propagates both
# their headers and their linked libraries transitively. posting and
# computed were direct links before persistence existed; now that
# ledgercore_persistence already PUBLIC-links both, listing them again
# here would only cause the linker to see each duplicated on the link
# line ("ignoring duplicate libraries"), exactly as domain/ledger/formula/
# trialbalance already would if listed directly. persistence and
# reporting remain direct links because nothing else in this list
# re-exports either of them.
target_link_libraries(ledgercore_cli
    PRIVATE
        ledgercore_persistence
        ledgercore_reporting
        ledgercore_warnings
)
