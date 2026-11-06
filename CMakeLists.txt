cmake_minimum_required(VERSION 3.22)
project(Sonicator VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
add_library(SonicatorCore STATIC core/Version.cpp resources/sqlite/sqlite3.c
    core/database/Database.cpp core/database/SampleRepository.cpp
    core/scanner/FileScanner.cpp core/dsp/BasicAnalyzer.cpp core/audio/AudioEngine.cpp core/audio/ContainerValidation.cpp
    core/indexing/IndexingService.cpp)
target_include_directories(SonicatorCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} resources/sqlite)
target_compile_definitions(SonicatorCore PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(SonicatorCore PUBLIC
    juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography)
juce_add_binary_data(SonicatorAssets SOURCES
    resources/branding/icon-header.png
    resources/branding/logo-silver.png)
add_library(SonicatorUI STATIC ui/MainComponent.cpp ui/library/FileListPanel.cpp ui/waveform/WaveformDisplay.cpp ui/preview/PreviewPanel.cpp)
target_link_libraries(SonicatorUI PUBLIC SonicatorCore PRIVATE SonicatorAssets)
juce_add_gui_app(Sonicator PRODUCT_NAME "SONICATOR" COMPANY_NAME "Sonicator"
    BUNDLE_ID "com.sonicator.browser" MICROPHONE_PERMISSION_ENABLED FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/icon-tile-1024.png")
target_sources(Sonicator PRIVATE app/Main.cpp)
target_link_libraries(Sonicator PRIVATE SonicatorUI)
if(APPLE)
    # The linker's signature covers only the Mach-O; seal the complete local app bundle.
    # This is an ad-hoc development signature, not Developer ID signing/notarization.
    add_custom_command(TARGET Sonicator POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DAPP_BUNDLE=$<TARGET_BUNDLE_DIR:Sonicator>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PrepareMacBundle.cmake"
        COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:Sonicator>"
        VERBATIM)
endif()
include(CTest)
if(BUILD_TESTING)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    add_subdirectory(tests)
endif()
