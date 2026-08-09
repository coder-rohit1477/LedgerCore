add_executable(ledgercore_reporting_tests
    BalanceSheetTest.cpp
    IncomeStatementTest.cpp
)

target_link_libraries(ledgercore_reporting_tests
    PRIVATE
        ledgercore_reporting
        ledgercore_posting
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_reporting_tests)
