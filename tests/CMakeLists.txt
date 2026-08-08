include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# Windows: keep GoogleTest's runtime library choice consistent with this project.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(ledgercore_smoke_tests
    smoke_test.cpp
)

target_link_libraries(ledgercore_smoke_tests
    PRIVATE
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_smoke_tests)

add_subdirectory(domain)
add_subdirectory(ledger)
add_subdirectory(posting)
add_subdirectory(trialbalance)
add_subdirectory(formula)
