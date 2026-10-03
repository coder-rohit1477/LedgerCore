# ledgercore_closing is linked to create genuine closing entries for the
# kind filter (it already re-exports posting, ledger, and domain).
add_executable(ledgercore_journalquery_tests
    JournalQueryTest.cpp
)

target_link_libraries(ledgercore_journalquery_tests
    PRIVATE
        ledgercore_journalquery
        ledgercore_closing
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_journalquery_tests)
