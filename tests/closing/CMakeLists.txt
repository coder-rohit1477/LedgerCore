# ledgercore_reporting is linked for the report-consistency tests; it and
# ledgercore_closing between them already re-export domain, ledger,
# posting, and trialbalance, so none of those is listed again.
add_executable(ledgercore_closing_tests
    ClosingEngineTest.cpp
)

target_link_libraries(ledgercore_closing_tests
    PRIVATE
        ledgercore_closing
        ledgercore_reporting
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_closing_tests)
