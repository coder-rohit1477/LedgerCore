add_executable(ledgercore_formula_tests
    LexerTest.cpp
    RationalTest.cpp
    ParserTest.cpp
    EvaluatorTest.cpp
)

target_link_libraries(ledgercore_formula_tests
    PRIVATE
        ledgercore_formula
        GTest::gtest_main
        ledgercore_warnings
)

gtest_discover_tests(ledgercore_formula_tests)
