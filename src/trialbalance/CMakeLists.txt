add_library(ledgercore_trialbalance STATIC
    src/TrialBalance.cpp
)

target_include_directories(ledgercore_trialbalance
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ledgercore_trialbalance
    PUBLIC
        ledgercore_domain
        ledgercore_ledger
)

ledgercore_target_warnings(ledgercore_trialbalance)
