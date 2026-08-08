add_library(ledgercore_domain STATIC
    src/Account.cpp
    src/ChartOfAccounts.cpp
)

target_include_directories(ledgercore_domain
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ledgercore_domain
    PRIVATE
        ledgercore_warnings
)
