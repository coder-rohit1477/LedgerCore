add_library(ledgercore_formula STATIC
    src/Rational.cpp
    src/Lexer.cpp
    src/Parser.cpp
    src/Evaluator.cpp
)

target_include_directories(ledgercore_formula
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ledgercore_formula
    PUBLIC
        ledgercore_domain
    PRIVATE
        ledgercore_warnings
)
