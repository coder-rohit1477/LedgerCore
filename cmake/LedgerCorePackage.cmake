# Installable, relocatable CMake package for the LedgerCore engine library:
#
#   find_package(LedgerCore 1.1 CONFIG REQUIRED)
#   target_link_libraries(my_app PRIVATE LedgerCore::ledgercore)
#
# Installed layout (relative to the install prefix):
#   include/ledgercore/<module>/*.h          public headers, as in the source tree
#   lib/libledgercore_<module>.a             the module libraries
#   lib/cmake/LedgerCore/LedgerCoreConfig.cmake, ...ConfigVersion.cmake,
#                        LedgerCoreTargets*.cmake
#
# Every path in the exported targets is relative to the package's own
# location, so the installed tree can be moved or copied anywhere. The
# exported targets carry only public include directories, the C++17
# requirement, and the module libraries' link order -- no warning flags,
# sanitizer flags, GoogleTest, or CLI code.
#
# The build tree is exported too: pointing LedgerCore_DIR at a LedgerCore
# build directory lets a project use it without installing (that tree
# refers to the source and build directories, so it is not relocatable).

include(CMakePackageConfigHelpers)

set(LEDGERCORE_PACKAGE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/LedgerCore")

install(TARGETS ${LEDGERCORE_MODULE_LIBRARIES} ledgercore
    EXPORT LedgerCoreTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)

# Each module's include/ directory holds only public headers
# (include/ledgercore/<module>/...); implementation headers live in src/.
foreach(module_library IN LISTS LEDGERCORE_MODULE_LIBRARIES)
    string(REPLACE "ledgercore_" "" module "${module_library}")
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/${module}/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    )
endforeach()

install(EXPORT LedgerCoreTargets
    NAMESPACE LedgerCore::
    DESTINATION "${LEDGERCORE_PACKAGE_INSTALL_DIR}"
)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/LedgerCoreConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/LedgerCoreConfig.cmake"
    INSTALL_DESTINATION "${LEDGERCORE_PACKAGE_INSTALL_DIR}"
)

# Version from project(); 1.x releases are compatible with each other.
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/LedgerCoreConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${PROJECT_BINARY_DIR}/LedgerCoreConfig.cmake"
    "${PROJECT_BINARY_DIR}/LedgerCoreConfigVersion.cmake"
    DESTINATION "${LEDGERCORE_PACKAGE_INSTALL_DIR}"
)

export(EXPORT LedgerCoreTargets
    NAMESPACE LedgerCore::
    FILE "${PROJECT_BINARY_DIR}/LedgerCoreTargets.cmake"
)
