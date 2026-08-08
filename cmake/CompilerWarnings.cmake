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
