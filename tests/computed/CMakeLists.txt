add_executable(ledgercore_computed_tests
    ComputedAccountRegistryTest.cpp
    LedgerAccountResolverTest.cpp
)

target_link_libraries(ledgercore_computed_tests
    PRIVATE
        ledgercore_computed
        ledgercore_posting
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_computed_tests)
