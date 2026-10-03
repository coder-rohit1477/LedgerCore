# Allocation-failure (fault-injection) tests for every mutating operation.
# This executable replaces the global operator new, so it is kept separate
# from the other test binaries.
add_executable(ledgercore_exceptionsafety_tests
    AllocationFailureTest.cpp
)

target_link_libraries(ledgercore_exceptionsafety_tests
    PRIVATE
        ledgercore_computed
        ledgercore_posting
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_exceptionsafety_tests)
