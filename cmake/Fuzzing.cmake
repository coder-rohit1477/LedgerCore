# Opt-in coverage-guided fuzzing with LLVM libFuzzer (Clang only):
#   cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DLEDGERCORE_BUILD_FUZZERS=ON
#
# OFF by default; normal builds are completely unaffected. When ON, the
# whole build directory is instrumented -- every LedgerCore library gets
# libFuzzer's coverage instrumentation plus AddressSanitizer and
# UndefinedBehaviorSanitizer, so the fuzz targets in fuzz/ see coverage
# from the code they exercise and any memory error or undefined behavior
# aborts the run. A fuzzing build is for fuzzing only: its libraries are
# instrumented, so the LedgerCore package is not installed from it.
option(LEDGERCORE_BUILD_FUZZERS "Build the libFuzzer fuzz targets (Clang only; instruments the whole build)" OFF)

if (LEDGERCORE_BUILD_FUZZERS)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=fuzzer")
    check_cxx_source_compiles(
        "#include <cstddef>
         #include <cstdint>
         extern \"C\" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }"
        LEDGERCORE_HAVE_LIBFUZZER)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if (NOT LEDGERCORE_HAVE_LIBFUZZER)
        message(FATAL_ERROR
            "LEDGERCORE_BUILD_FUZZERS requires a Clang with the libFuzzer runtime "
            "(-fsanitize=fuzzer); ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} cannot link it. "
            "On Ubuntu, install clang and libclang-rt-<version>-dev.")
    endif()

    add_compile_options(
        -fsanitize=fuzzer-no-link,address,undefined
        -fno-sanitize-recover=undefined
        -fno-omit-frame-pointer
        -g
    )
    add_link_options(-fsanitize=address,undefined)
endif()
