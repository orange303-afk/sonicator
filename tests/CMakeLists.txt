add_executable(SonicatorTests TestScaffold.cpp TestDatabase.cpp TestScanner.cpp TestAnalyzer.cpp)
target_link_libraries(SonicatorTests PRIVATE SonicatorCore Catch2::Catch2WithMain)
add_test(NAME SonicatorTests COMMAND SonicatorTests)
