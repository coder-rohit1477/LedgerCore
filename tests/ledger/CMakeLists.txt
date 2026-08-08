add_executable(ledgercore_ledger_tests
    LedgerTest.cpp
)

target_link_libraries(ledgercore_ledger_tests
    PRIVATE
        ledgercore_ledger
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_ledger_tests)
