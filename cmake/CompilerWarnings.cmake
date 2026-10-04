# INTERFACE target carrying the project-wide warning policy.
# Any target that wants these warnings links against it with:
#   target_link_libraries(<target> PRIVATE ledgercore_warnings)
add_library(ledgercore_warnings INTERFACE)

if (MSVC)
    target_compile_options(ledgercore_warnings INTERFACE
        /W4
        /permissive-
    )
else()
    target_compile_options(ledgercore_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )
endif()

# Applies the same warning policy to an installable library as PRIVATE
# compile options. Those libraries do not link ledgercore_warnings: CMake
# records a static library's private link dependencies in its exported link
# interface, which would make the LedgerCore package depend on this
# build-only target.
function(ledgercore_target_warnings target)
    target_compile_options(${target} PRIVATE $<TARGET_PROPERTY:ledgercore_warnings,INTERFACE_COMPILE_OPTIONS>)
endfunction()
