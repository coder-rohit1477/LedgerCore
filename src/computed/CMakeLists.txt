add_library(ledgercore_computed STATIC
    src/ComputedAccountDefinition.cpp
    src/LedgerAccountResolver.cpp
    src/ComputedAccountRegistry.cpp
)

target_include_directories(ledgercore_computed
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ledgercore_computed
    PUBLIC
        ledgercore_domain
        ledgercore_ledger
        ledgercore_formula
    PRIVATE
        ledgercore_warnings
)
