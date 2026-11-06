add_executable(SonicatorTests TestMain.cpp TestScaffold.cpp TestDatabase.cpp TestScanner.cpp TestAnalyzer.cpp TestAudioEngine.cpp TestIndexing.cpp TestUI.cpp TestShell.cpp TestScale.cpp TestFormats.cpp TestExternalLibrary.cpp)
target_link_libraries(SonicatorTests PRIVATE SonicatorUI Catch2::Catch2)
target_compile_definitions(SonicatorTests PRIVATE SONICATOR_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME SonicatorTests COMMAND SonicatorTests)
