add_executable(ledgercore_domain_tests
    AccountTest.cpp
    ChartOfAccountsTest.cpp
    MoneyTest.cpp
)

target_link_libraries(ledgercore_domain_tests
    PRIVATE
        ledgercore_domain
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_domain_tests)
