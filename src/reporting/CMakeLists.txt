add_library(ledgercore_reporting STATIC
    src/BalanceSheet.cpp
    src/IncomeStatement.cpp
)

target_include_directories(ledgercore_reporting
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ledgercore_reporting
    PUBLIC
        ledgercore_domain
        ledgercore_trialbalance
    PRIVATE
        ledgercore_warnings
)
