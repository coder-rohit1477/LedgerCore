add_executable(ledgercore_cli_unit_tests
    InputParsingTest.cpp
    CommandParserTest.cpp
    LedgerSessionTest.cpp
)

target_link_libraries(ledgercore_cli_unit_tests
    PRIVATE
        ledgercore_cli_logic
        ledgercore_test_support
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
        ledgercore_test_support
        GTest::gtest_main
        ledgercore_warnings
)

target_compile_definitions(ledgercore_cli_e2e_tests
    PRIVATE
        LEDGERCORE_CLI_PATH="$<TARGET_FILE:ledgercore_cli>"
        LEDGERCORE_DEMO_SCRIPT="${PROJECT_SOURCE_DIR}/examples/demo.txt"
        LEDGERCORE_EXPECTED_VERSION="${PROJECT_VERSION}"
)

add_dependencies(ledgercore_cli_e2e_tests ledgercore_cli)

gtest_discover_tests(ledgercore_cli_e2e_tests)
