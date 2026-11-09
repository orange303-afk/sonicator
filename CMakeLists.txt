cmake_minimum_required(VERSION 3.22)
project(Sonicator VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
include(FetchContent)
find_package(ZLIB REQUIRED)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.5 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)
# libxmp plays legacy tracker modules (MOD/XM/S3M and related formats). It is built as a static
# C library through its own CMake project; depackers and the ProWizard loader are disabled so a
# module is never unpacked from an embedded archive or guessed from packed headers.
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(BUILD_LITE OFF CACHE BOOL "" FORCE)
set(LIBXMP_DISABLE_DEPACKERS ON CACHE BOOL "" FORCE)
set(LIBXMP_DISABLE_PROWIZARD ON CACHE BOOL "" FORCE)
set(LIBXMP_PIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(libxmp
    GIT_REPOSITORY https://github.com/libxmp/libxmp.git
    GIT_TAG libxmp-4.6.2 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(libxmp)
# libxmp enables verbose stderr tracing through -DDEBUG in Debug builds. Keep the library silent:
# its diagnostics are development noise for an application and would otherwise dominate tests.
if(NOT MSVC)
    target_compile_options(xmp_static PRIVATE $<$<CONFIG:Debug>:-UDEBUG>)
endif()
add_library(SonicatorCore STATIC core/Version.cpp resources/sqlite/sqlite3.c
    core/database/Database.cpp core/database/PathRelocation.cpp core/database/SearchSyntax.cpp core/database/SampleRepository.cpp core/settings/AppSettings.cpp
    core/scanner/FileScanner.cpp core/dsp/BasicAnalyzer.cpp core/dsp/RhythmAnalyzer.cpp core/audio/AudioEngine.cpp core/audio/MidiSampler.cpp core/audio/SampleAudition.cpp core/audio/ContainerValidation.cpp
    core/export/AudioExporter.cpp core/export/SelectionExporter.cpp core/export/ExternalFileDrag.cpp
    core/extract/EmbeddedSamples.cpp core/extract/ExtractService.cpp
    core/indexing/IndexingService.cpp core/split/SplitService.cpp core/split/SilenceDetector.cpp
    core/tagging/TaggingService.cpp core/tagging/AutoTagRules.cpp core/tagging/AutoTagService.cpp
    core/smart/SmartTypes.cpp core/smart/KeyNames.cpp core/smart/SmartResolver.cpp core/smart/NameHeuristics.cpp core/smart/WavMetadata.cpp core/smart/AiffMetadata.cpp core/smart/AbletonAsd.cpp core/smart/Rex2Metadata.cpp core/smart/SmartAnalysisService.cpp core/smart/AssetAnalyzer.cpp core/smart/RhythmCandidates.cpp
    core/sf2/Sf2Bank.cpp core/sf2/Sf2KitSampler.cpp
    core/reason/ReasonPatch.cpp
    core/ableton/AbletonPacks.cpp
    core/sampler/FileInstrument.cpp core/sampler/ExternalKitSampler.cpp
    core/tracker/TrackerFormats.cpp core/tracker/TrackerModReader.cpp core/tracker/TrackerXmReader.cpp
    core/tracker/TrackerS3mReader.cpp core/tracker/TrackerDocument.cpp core/tracker/TrackerSongRenderer.cpp
    core/audio/SampleVoicePlayer.cpp
    core/audio/StereoPair.cpp
    core/legacy/LegacyFormatProbe.cpp
    core/disc/RawDiscSource.cpp
    core/disc/SectorLayout.cpp
    core/disc/DiscImageProbe.cpp
    core/disc/Iso9660Filesystem.cpp
    core/disc/DiscAudioReader.cpp
    core/disc/AkaiFilesystem.cpp
    core/disc/DiscBrowserService.cpp
    core/disc/CueSheet.cpp
    core/disc/AkaiSampleReader.cpp
    core/disc/DiscContentIndexer.cpp
    core/rex/Rex2Decoder.cpp core/rex/Rex2SlicePlayer.cpp)
target_include_directories(SonicatorCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} resources/sqlite)
target_compile_definitions(SonicatorCore PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(SonicatorCore PUBLIC
    juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography ZLIB::ZLIB xmp_static)
juce_add_binary_data(SonicatorAssets SOURCES
    resources/branding/logo-silver.png)
add_library(SonicatorUI STATIC ui/MainComponent.cpp ui/library/FileListPanel.cpp ui/library/FolderTree.cpp ui/library/TagStrip.cpp ui/waveform/WaveformDisplay.cpp ui/preview/PreviewPanel.cpp ui/preview/SongProgress.cpp ui/tagging/AutoTagDialog.cpp ui/split/SplitDialog.cpp ui/extract/ExtractDialog.cpp ui/shell/ThemedDialogs.cpp ui/settings/SettingsPanel.cpp ui/settings/RelocateFoldersPanel.cpp ui/shell/WindowBounds.cpp ui/disc/DiscBrowserPanel.cpp)
if(APPLE)
    target_sources(SonicatorUI PRIVATE ui/platform/FileClipboard.mm)
else()
    target_sources(SonicatorUI PRIVATE ui/platform/FileClipboard.cpp)
endif()
target_link_libraries(SonicatorUI PUBLIC SonicatorCore PRIVATE SonicatorAssets)
juce_add_gui_app(Sonicator PRODUCT_NAME "SONICATOR" COMPANY_NAME "Sonicator"
    BUNDLE_ID "com.sonicator.browser" MICROPHONE_PERMISSION_ENABLED FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/icon-glossy-chevron-1024.png")
target_sources(Sonicator PRIVATE app/Main.cpp)
target_link_libraries(Sonicator PRIVATE SonicatorUI)
if(APPLE)
    # The linker's signature covers only the Mach-O; seal the complete local app bundle.
    # This is an ad-hoc development signature, not Developer ID signing/notarization.
    add_custom_command(TARGET Sonicator POST_BUILD
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignMacBundle.sh"
            "$<TARGET_BUNDLE_DIR:Sonicator>"
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
