add_library(ledgercore_journalquery STATIC
    src/JournalQuery.cpp
)

target_include_directories(ledgercore_journalquery
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# A read-only projection of Ledger history: needs only the domain types and
# the Ledger's posted-entry history -- no chart, posting, or trial balance.
target_link_libraries(ledgercore_journalquery
    PUBLIC
        ledgercore_domain
        ledgercore_ledger
)

ledgercore_target_warnings(ledgercore_journalquery)
