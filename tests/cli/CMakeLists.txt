# Test-only static library built from the CLI's own logic sources (every
# src/cli/src/*.cpp file except main.cpp, which defines main() and would
# conflict with GTest's). This is not a production target -- it exists
# only so InputParsingTest/CommandParserTest can link the parsing logic
# directly without spawning a process, and it introduces no change to
# src/cli's own CMakeLists.txt (still an executable-only target with no
# public library of its own).
add_library(ledgercore_cli_logic STATIC
    ${CMAKE_SOURCE_DIR}/src/cli/src/LedgerSession.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/src/CommandParser.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/src/InputParsing.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/src/OutputFormatting.cpp
)

target_include_directories(ledgercore_cli_logic
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/cli/src
)

target_link_libraries(ledgercore_cli_logic
    PUBLIC
        ledgercore_domain
        ledgercore_ledger
        ledgercore_posting
        ledgercore_trialbalance
        ledgercore_formula
        ledgercore_computed
        ledgercore_reporting
    PRIVATE
        ledgercore_warnings
)

add_executable(ledgercore_cli_unit_tests
    InputParsingTest.cpp
    CommandParserTest.cpp
)

target_link_libraries(ledgercore_cli_unit_tests
    PRIVATE
        ledgercore_cli_logic
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_cli_unit_tests)

# Process-level end-to-end tests: builds and runs the real ledgercore_cli
# executable via --script mode and checks its stdout/exit code, so it
# needs the built binary's path rather than a link dependency.
add_executable(ledgercore_cli_e2e_tests
    CliEndToEndTest.cpp
)

target_link_libraries(ledgercore_cli_e2e_tests
    PRIVATE
        GTest::gtest_main
        ledgercore_warnings
)

target_compile_definitions(ledgercore_cli_e2e_tests
    PRIVATE
        LEDGERCORE_CLI_PATH="$<TARGET_FILE:ledgercore_cli>"
)

add_dependencies(ledgercore_cli_e2e_tests ledgercore_cli)

gtest_discover_tests(ledgercore_cli_e2e_tests)
