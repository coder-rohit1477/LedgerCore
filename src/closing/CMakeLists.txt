add_library(ledgercore_closing STATIC
    src/ClosingEngine.cpp
)

target_include_directories(ledgercore_closing
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# closing sits above both posting (it posts through posting::post) and
# trialbalance (it reads as-of balances through TrialBalance) -- posting
# itself must not depend on trialbalance, so this is its own module.
target_link_libraries(ledgercore_closing
    PUBLIC
        ledgercore_domain
        ledgercore_ledger
        ledgercore_posting
        ledgercore_trialbalance
)

ledgercore_target_warnings(ledgercore_closing)
