# The CLI's command layer (parsing, execution, session, formatting) as an
# internal static library, shared by the ledgercore executable, the CLI
# tests, and the fuzz targets. It is neither installed nor exported; the
# executable adds only main.cpp (argument handling, REPL, script mode).
add_library(ledgercore_cli_logic STATIC
    src/CommandExecution.cpp
    src/LedgerSession.cpp
    src/CommandParser.cpp
    src/InputParsing.cpp
    src/OutputFormatting.cpp
)

target_include_directories(ledgercore_cli_logic
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# domain, ledger, formula, and trialbalance are deliberately not listed
# here even though the CLI uses their types directly: each is already a
# PUBLIC dependency of at least one target below (persistence ->
# domain+ledger+posting+computed; computed -> domain+ledger+formula;
# reporting -> domain+trialbalance), so CMake already propagates both
# their headers and their linked libraries transitively; listing them
# again would only make the linker see each duplicated on the link line
# ("ignoring duplicate libraries"). persistence, reporting, closing, and
# journalquery are direct links because nothing else in this list
# re-exports any of them.
target_link_libraries(ledgercore_cli_logic
    PUBLIC
        ledgercore_persistence
        ledgercore_reporting
        ledgercore_closing
        ledgercore_journalquery
)

ledgercore_target_warnings(ledgercore_cli_logic)

add_executable(ledgercore_cli
    src/main.cpp
)

set_target_properties(ledgercore_cli PROPERTIES OUTPUT_NAME ledgercore)

# `ledgercore --version` prints the project() version; this definition is
# its only source, so the CLI cannot drift from the CMake/package version.
target_compile_definitions(ledgercore_cli PRIVATE LEDGERCORE_VERSION="${PROJECT_VERSION}")

# `cmake --install` installs the ledgercore executable. The engine libraries
# are installed separately, as the LedgerCore CMake package
# (cmake/LedgerCorePackage.cmake).
install(TARGETS ledgercore_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

target_link_libraries(ledgercore_cli
    PRIVATE
        ledgercore_cli_logic
        ledgercore_warnings
)
