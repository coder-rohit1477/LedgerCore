add_library(ledgercore_trialbalance STATIC
    src/TrialBalance.cpp
)

target_include_directories(ledgercore_trialbalance
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ledgercore_trialbalance
    PUBLIC
        ledgercore_domain
        ledgercore_ledger
    PRIVATE
        ledgercore_warnings
)
