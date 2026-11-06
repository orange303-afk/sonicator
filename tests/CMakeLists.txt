add_executable(SonicatorTests TestScaffold.cpp TestDatabase.cpp)
target_link_libraries(SonicatorTests PRIVATE SonicatorCore Catch2::Catch2WithMain)
add_test(NAME SonicatorTests COMMAND SonicatorTests)
