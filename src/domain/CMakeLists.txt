add_library(ledgercore_domain STATIC
    src/Account.cpp
    src/ChartOfAccounts.cpp
    src/Money.cpp
    src/JournalEntry.cpp
    src/NormalBalance.cpp
    src/DateFormatting.cpp
)

target_include_directories(ledgercore_domain
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

ledgercore_target_warnings(ledgercore_domain)

# The public headers use C++17. Every LedgerCore library links
# ledgercore_domain PUBLIC, so this requirement reaches any target that
# links a LedgerCore library -- including projects that pull LedgerCore in
# with add_subdirectory()/FetchContent and use another default standard.
target_compile_features(ledgercore_domain PUBLIC cxx_std_17)
