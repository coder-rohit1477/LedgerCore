add_library(ledgercore_formula STATIC
    src/Rational.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/Evaluator.cpp
)

target_include_directories(ledgercore_formula
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(ledgercore_formula
    PUBLIC
        ledgercore_domain
)

ledgercore_target_warnings(ledgercore_formula)
