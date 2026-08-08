add_executable(ledgercore_trialbalance_tests
    TrialBalanceTest.cpp
)

target_link_libraries(ledgercore_trialbalance_tests
    PRIVATE
        ledgercore_trialbalance
        ledgercore_posting
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_trialbalance_tests)
