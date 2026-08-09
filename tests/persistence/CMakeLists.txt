# ledgercore_reporting is linked (not ledgercore_trialbalance directly) --
# reporting already PUBLIC-links trialbalance, and ledgercore_persistence
# already PUBLIC-links domain/ledger/posting/computed (which itself
# PUBLIC-links formula). Linking either transitively-available target again
# directly here would just reintroduce the "ignoring duplicate libraries"
# redundancy already fixed once in the CLI's own CMakeLists.txt.
add_executable(ledgercore_persistence_tests
    SessionStoreTest.cpp
)

target_link_libraries(ledgercore_persistence_tests
    PRIVATE
        ledgercore_persistence
        ledgercore_reporting
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_persistence_tests)
