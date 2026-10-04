# Script-mode (cmake -P) driver for the package tests registered in
# tests/package/CMakeLists.txt. MODE selects what is verified:
#
#   install     cmake --install the already-built LedgerCore build tree into
#               a temporary prefix, move that tree to a second prefix
#               (deleting the original, so nothing can still point at it),
#               audit it, then configure, build, and run the external
#               consumer against the relocated package. Also checks that the
#               package rejects an incompatible requested version.
#   build-tree  configure, build, and run the consumer against the build
#               tree's exported package (LedgerCore_DIR), without installing.
#
# Inputs (-D): MODE, LEDGERCORE_SOURCE_DIR, LEDGERCORE_BINARY_DIR, WORK_DIR,
# CONSUMER_SOURCE_DIR, GENERATOR, CXX_COMPILER, BUILD_TYPE,
# EXPECTED_VERSION, CONSUMER_FLAGS (extra compile+link flags, e.g. for a
# sanitizer build of LedgerCore).

function(run_step description)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
    if (NOT result EQUAL 0)
        message(FATAL_ERROR "${description} failed (${result}):\n${output}")
    endif()
    set(step_output "${output}" PARENT_SCOPE)
endfunction()

# Configures (expecting success or failure), builds, and runs the consumer.
function(run_consumer build_dir expect_configure_success)
    file(REMOVE_RECURSE "${build_dir}")
    set(configure_command "${CMAKE_COMMAND}" -S "${CONSUMER_SOURCE_DIR}" -B "${build_dir}" -G "${GENERATOR}"
        "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
        "-DCMAKE_CXX_FLAGS=${CONSUMER_FLAGS}" "-DCMAKE_EXE_LINKER_FLAGS=${CONSUMER_FLAGS}"
        # Only the package under test may be found.
        -DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
        ${ARGN})
    if (NOT expect_configure_success)
        execute_process(COMMAND ${configure_command} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE output)
        if (result EQUAL 0)
            message(FATAL_ERROR "Consumer configure unexpectedly succeeded with ${ARGN}:\n${output}")
        endif()
        return()
    endif()
    run_step("Consumer configure" ${configure_command})
    run_step("Consumer build" "${CMAKE_COMMAND}" --build "${build_dir}" --config "${BUILD_TYPE}")
    file(GLOB_RECURSE consumer_executable "${build_dir}/ledgercore_consumer" "${build_dir}/*/ledgercore_consumer")
    if (NOT consumer_executable)
        message(FATAL_ERROR "Consumer executable not found under ${build_dir}")
    endif()
    list(GET consumer_executable 0 consumer_executable)
    run_step("Consumer run" "${consumer_executable}" "${build_dir}")
    if (NOT step_output MATCHES "LedgerCore consumer OK")
        message(FATAL_ERROR "Consumer did not report success:\n${step_output}")
    endif()
endfunction()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

if (MODE STREQUAL "build-tree")
    run_consumer("${WORK_DIR}/consumer" TRUE
        "-DLedgerCore_DIR=${LEDGERCORE_BINARY_DIR}" "-DLEDGERCORE_EXPECTED_VERSION=${EXPECTED_VERSION}")
    message(STATUS "Build-tree package consumer built and ran")
    return()
endif()

if (NOT MODE STREQUAL "install")
    message(FATAL_ERROR "Unknown MODE '${MODE}'")
endif()

set(original_prefix "${WORK_DIR}/original-prefix")
set(relocated_prefix "${WORK_DIR}/relocated-prefix")
run_step("Install" "${CMAKE_COMMAND}" --install "${LEDGERCORE_BINARY_DIR}" --prefix "${original_prefix}"
    --config "${BUILD_TYPE}")
file(RENAME "${original_prefix}" "${relocated_prefix}")

# --- Audit the installed tree -------------------------------------------
file(GLOB_RECURSE installed_files RELATIVE "${relocated_prefix}" "${relocated_prefix}/*")
foreach(installed_file IN LISTS installed_files)
    if (NOT installed_file MATCHES "^(bin/ledgercore(\\.exe)?|include/ledgercore/[a-z]+/[A-Za-z]+\\.h|lib(64)?/(lib)?ledgercore_[a-z]+\\.(a|lib)|lib(64)?/cmake/LedgerCore/LedgerCore[A-Za-z-]*\\.cmake)$")
        message(FATAL_ERROR "Unexpected file in the install tree: ${installed_file}")
    endif()
endforeach()
file(GLOB package_dir "${relocated_prefix}/lib*/cmake/LedgerCore")
foreach(required LedgerCoreConfig.cmake LedgerCoreConfigVersion.cmake LedgerCoreTargets.cmake)
    if (NOT EXISTS "${package_dir}/${required}")
        message(FATAL_ERROR "Package file ${required} not installed")
    endif()
endforeach()
file(STRINGS "${package_dir}/LedgerCoreConfigVersion.cmake" version_line REGEX "set\\(PACKAGE_VERSION ")
if (NOT version_line MATCHES "\"${EXPECTED_VERSION}\"")
    message(FATAL_ERROR "Package version is not ${EXPECTED_VERSION}: ${version_line}")
endif()
file(GLOB package_files "${package_dir}/*.cmake")
foreach(package_file IN LISTS package_files)
    file(READ "${package_file}" content)
    foreach(forbidden "${LEDGERCORE_SOURCE_DIR}" "${LEDGERCORE_BINARY_DIR}" "${original_prefix}" "${WORK_DIR}")
        string(FIND "${content}" "${forbidden}" position)
        if (NOT position EQUAL -1)
            message(FATAL_ERROR "${package_file} contains the absolute path ${forbidden}")
        endif()
    endforeach()
    if (content MATCHES "GTest|gtest|gmock|ledgercore_warnings|ledgercore_cli|-fsanitize")
        message(FATAL_ERROR "${package_file} references a build- or test-only dependency")
    endif()
endforeach()

# --- Consume the relocated package --------------------------------------
run_consumer("${WORK_DIR}/consumer" TRUE
    "-DCMAKE_PREFIX_PATH=${relocated_prefix}" "-DLEDGERCORE_EXPECTED_VERSION=${EXPECTED_VERSION}"
    "-DLEDGERCORE_REQUESTED_VERSION=1.0")

# SameMajorVersion: a request for 2.0 must not be satisfied by 1.x.
run_consumer("${WORK_DIR}/consumer-incompatible" FALSE
    "-DCMAKE_PREFIX_PATH=${relocated_prefix}" "-DLEDGERCORE_REQUESTED_VERSION=2.0")

message(STATUS "Relocated installed package audited, consumed, and version-checked")
