add_executable(ledgercore_domain_tests
    AccountTest.cpp
    ChartOfAccountsTest.cpp
    MoneyTest.cpp
    JournalEntryTest.cpp
    NormalBalanceTest.cpp
    PeriodTest.cpp
)

# ledgercore_posting (not ledgercore_domain directly): attaching a child
# account is only reachable through posting::addChildAccount() -- see
# ChartOfAccounts.h -- so the chart-shape tests here need it. posting
# already PUBLIC-links domain and ledger, so listing domain again would
# only duplicate it on the link line. This is test-only: the domain
# library itself still links against nothing project-internal.
target_link_libraries(ledgercore_domain_tests
    PRIVATE
        ledgercore_posting
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_domain_tests)
