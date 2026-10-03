# Opt-in AddressSanitizer + UndefinedBehaviorSanitizer build:
#   cmake -S . -B build-sanitize -DLEDGERCORE_SANITIZE=ON
#
# OFF by default, so normal builds are completely unaffected. When ON, the
# flags are applied directory-wide (every library, executable, and the
# fetched GoogleTest) rather than through ledgercore_warnings: sanitizer
# runtimes must be linked into the final executables, and mixing
# instrumented and uninstrumented code in one process weakens ASan's
# checks. -fno-sanitize-recover=undefined makes any UB report abort the
# process, so CTest reports it as a failing test instead of a log line.
option(LEDGERCORE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if (LEDGERCORE_SANITIZE)
    if (MSVC)
        message(FATAL_ERROR "LEDGERCORE_SANITIZE requires GCC or Clang (ASan+UBSan)")
    endif()

    set(LEDGERCORE_SANITIZER_FLAGS
        -fsanitize=address,undefined
        -fno-sanitize-recover=undefined
        -fno-omit-frame-pointer
    )
    add_compile_options(${LEDGERCORE_SANITIZER_FLAGS} -g)
    add_link_options(-fsanitize=address,undefined)
endif()
