add_library(ledgercore_posting STATIC
    src/PostingEngine.cpp
)

target_include_directories(ledgercore_posting
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ledgercore_posting
    PUBLIC
        ledgercore_domain
        ledgercore_ledger
    PRIVATE
        ledgercore_warnings
)
