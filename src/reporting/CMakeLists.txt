add_library(ledgercore_reporting STATIC
    src/BalanceSheet.cpp
    src/IncomeStatement.cpp
)

target_include_directories(ledgercore_reporting
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ledgercore_reporting
    PUBLIC
        ledgercore_domain
        ledgercore_trialbalance
)

ledgercore_target_warnings(ledgercore_reporting)
