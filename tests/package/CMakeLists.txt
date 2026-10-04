# Package tests: an external project (consumer/) consumes LedgerCore
# through find_package(LedgerCore CONFIG) -- once from a relocated install
# tree and once from this build tree. Each test drives nested CMake runs via
# PackageConsumerTest.cmake; neither links GoogleTest.

# A sanitizer build of LedgerCore needs the consumer linked with the
# sanitizer runtimes too; the package itself never carries those flags.
if (LEDGERCORE_SANITIZE)
    set(consumer_flags "-fsanitize=address,undefined")
else()
    set(consumer_flags "")
endif()

foreach(mode install build-tree)
    add_test(NAME LedgerCorePackage.${mode}
        COMMAND "${CMAKE_COMMAND}"
            "-DMODE=${mode}"
            "-DLEDGERCORE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DLEDGERCORE_BINARY_DIR=${PROJECT_BINARY_DIR}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/${mode}"
            "-DCONSUMER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DBUILD_TYPE=$<CONFIG>"
            "-DEXPECTED_VERSION=${PROJECT_VERSION}"
            "-DEXECUTABLE_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}"
            "-DCONSUMER_FLAGS=${consumer_flags}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/PackageConsumerTest.cmake"
    )
    set_tests_properties(LedgerCorePackage.${mode} PROPERTIES TIMEOUT 600)
endforeach()
