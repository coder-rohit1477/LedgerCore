# libFuzzer fuzz targets (built only with LEDGERCORE_BUILD_FUZZERS; see
# cmake/Fuzzing.cmake). Not installed and not part of the package.
#
# Each target has a small seed corpus (corpus/<target>/) and a dictionary
# of grammar tokens (dictionaries/<target>.dict). CTest replays every seed
# once (LedgerCoreFuzz.<target>.corpus); fuzzing itself is run directly,
# e.g.  build-fuzz/fuzz/ledgercore_snapshot_fuzzer -max_total_time=60
#           -dict=fuzz/dictionaries/snapshot.dict <work-corpus-dir> fuzz/corpus/snapshot

function(ledgercore_add_fuzzer name source)
    add_executable(ledgercore_${name}_fuzzer ${source})
    target_link_options(ledgercore_${name}_fuzzer PRIVATE -fsanitize=fuzzer)
    target_link_libraries(ledgercore_${name}_fuzzer PRIVATE ${ARGN} ledgercore_warnings)
    add_test(NAME LedgerCoreFuzz.${name}.corpus
        COMMAND ledgercore_${name}_fuzzer -runs=0 ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${name})
endfunction()

ledgercore_add_fuzzer(snapshot SnapshotFuzzer.cpp ledgercore_persistence)
# SnapshotStream.h is a persistence-internal (not installed) header.
target_include_directories(ledgercore_snapshot_fuzzer PRIVATE ${PROJECT_SOURCE_DIR}/src/persistence/src)

ledgercore_add_fuzzer(formula FormulaFuzzer.cpp ledgercore_computed ledgercore_posting)

ledgercore_add_fuzzer(cli CliFuzzer.cpp ledgercore_cli_logic)
