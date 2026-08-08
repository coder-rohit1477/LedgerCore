add_executable(ledgercore_posting_tests
    PostingEngineTest.cpp
)

target_link_libraries(ledgercore_posting_tests
    PRIVATE
        ledgercore_posting
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_posting_tests)
