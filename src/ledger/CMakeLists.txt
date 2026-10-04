add_library(ledgercore_ledger STATIC
    src/Ledger.cpp
)

target_include_directories(ledgercore_ledger
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ledgercore_ledger
    PUBLIC
        ledgercore_domain
)

ledgercore_target_warnings(ledgercore_ledger)
