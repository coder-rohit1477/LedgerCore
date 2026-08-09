add_library(ledgercore_persistence STATIC
    src/SessionStore.cpp
)

target_include_directories(ledgercore_persistence
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ledgercore_persistence
    PUBLIC
        ledgercore_domain
        ledgercore_ledger
        ledgercore_posting
        ledgercore_computed
    PRIVATE
        ledgercore_warnings
)
