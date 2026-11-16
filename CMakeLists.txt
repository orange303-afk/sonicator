cmake_minimum_required(VERSION 3.22)
project(Sonicator VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
include(FetchContent)
# zlib is used by JUCE and by the NKI reader. macOS and Linux ship it with the SDK; Windows does not,
# so the build fetches the upstream archive there instead of requiring vcpkg or a manual install.
find_package(ZLIB)
if(NOT ZLIB_FOUND)
    FetchContent_Declare(zlib
        URL "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz"
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(zlib)
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    message(STATUS "zlib: system copy not found, using the fetched static build")
endif()
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
if(APPLE)
    FetchContent_Declare(
        onnxruntime
        URL "https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-osx-universal2-1.20.1.tgz"
    )
    FetchContent_MakeAvailable(onnxruntime)
    add_library(onnxruntime_lib SHARED IMPORTED GLOBAL)
    set_target_properties(onnxruntime_lib PROPERTIES
        IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.dylib"
        INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include"
    )
elseif(WIN32)
    FetchContent_Declare(
        onnxruntime
        URL "https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip"
    )
    FetchContent_MakeAvailable(onnxruntime)
    add_library(onnxruntime_lib SHARED IMPORTED GLOBAL)
    set_target_properties(onnxruntime_lib PROPERTIES
        IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll"
        IMPORTED_IMPLIB "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include"
    )
else()
    FetchContent_Declare(
        onnxruntime
        URL "https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-linux-x64-1.20.1.tgz"
    )
    FetchContent_MakeAvailable(onnxruntime)
    add_library(onnxruntime_lib SHARED IMPORTED GLOBAL)
    set_target_properties(onnxruntime_lib PROPERTIES
        IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.so"
        INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include"
    )
endif()
FetchContent_Declare(libxmp
    GIT_REPOSITORY https://github.com/libxmp/libxmp.git
    GIT_TAG libxmp-4.6.2 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(libxmp)
# LAME 3.100 provides the MP3 encoder. The pinned archive is compiled as a small static C
# library (encoder only; its mpglib decoder and the CLI frontend are not built). The configuration
# header is checked in for POSIX targets; MSVC uses the upstream configMS.h.
set(LAME_VERSION 3.100)
set(LAME_ARCHIVE ${CMAKE_BINARY_DIR}/lame-${LAME_VERSION}.tar.gz)
set(LAME_SOURCE ${CMAKE_BINARY_DIR}/lame-src)
if(NOT EXISTS "${LAME_SOURCE}/libmp3lame/lame.c")
    file(DOWNLOAD
        "https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz"
        "${LAME_ARCHIVE}"
        EXPECTED_HASH SHA256=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
        SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT "${LAME_ARCHIVE}" DESTINATION "${CMAKE_BINARY_DIR}/lame-extract")
    file(RENAME "${CMAKE_BINARY_DIR}/lame-extract/lame-${LAME_VERSION}" "${LAME_SOURCE}")
endif()
if(MSVC)
    configure_file("${LAME_SOURCE}/configMS.h" "${CMAKE_BINARY_DIR}/lame-config/config.h" COPYONLY)
else()
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/lame-config.h"
                   "${CMAKE_BINARY_DIR}/lame-config/config.h" COPYONLY)
endif()
add_library(lame_static STATIC
    "${LAME_SOURCE}/libmp3lame/VbrTag.c"
    "${LAME_SOURCE}/libmp3lame/bitstream.c"
    "${LAME_SOURCE}/libmp3lame/encoder.c"
    "${LAME_SOURCE}/libmp3lame/fft.c"
    "${LAME_SOURCE}/libmp3lame/gain_analysis.c"
    "${LAME_SOURCE}/libmp3lame/id3tag.c"
    "${LAME_SOURCE}/libmp3lame/lame.c"
    "${LAME_SOURCE}/libmp3lame/newmdct.c"
    "${LAME_SOURCE}/libmp3lame/presets.c"
    "${LAME_SOURCE}/libmp3lame/psymodel.c"
    "${LAME_SOURCE}/libmp3lame/quantize.c"
    "${LAME_SOURCE}/libmp3lame/quantize_pvt.c"
    "${LAME_SOURCE}/libmp3lame/reservoir.c"
    "${LAME_SOURCE}/libmp3lame/set_get.c"
    "${LAME_SOURCE}/libmp3lame/tables.c"
    "${LAME_SOURCE}/libmp3lame/takehiro.c"
    "${LAME_SOURCE}/libmp3lame/util.c"
    "${LAME_SOURCE}/libmp3lame/vbrquantize.c"
    "${LAME_SOURCE}/libmp3lame/version.c")
target_include_directories(lame_static PUBLIC
    "${LAME_SOURCE}/libmp3lame" "${LAME_SOURCE}/include"
    PRIVATE "${CMAKE_BINARY_DIR}/lame-config")
if(MSVC)
    # configMS.h defines HAVE_XMMINTRIN_H on x64, so quantize.c calls the SSE variant that lives in
    # vector/xmm_quantize_sub.c. Upstream's other build systems compile that file; without it the
    # link fails with an unresolved init_xrpow_core_sse.
    target_sources(lame_static PRIVATE "${LAME_SOURCE}/libmp3lame/vector/xmm_quantize_sub.c")
endif()
# Upstream ships and tests the library with assertions off; the CBR quantizer has internal
# invariants that assert under a Debug build even for ordinary audio.
target_compile_definitions(lame_static PRIVATE HAVE_CONFIG_H=1 NDEBUG=1)
set_target_properties(lame_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_compile_options(lame_static PRIVATE -w)
    target_link_libraries(lame_static PUBLIC m)
endif()

# libsamplerate provides the band-limited conversion used by Batch Processing. The archive is
# pinned by SHA256 and built statically; its examples, tests and install rules stay off.
set(SONICATOR_BUILD_TESTING_SAVED ${BUILD_TESTING})
# The dependency's own test tree must not join Sonicator's ctest: force the cache value off for
# its configure scope and restore the project's value afterwards.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LIBSAMPLERATE_INSTALL OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_Declare(libsamplerate
    URL "https://github.com/libsndfile/libsamplerate/archive/refs/tags/0.2.2.tar.gz"
    URL_HASH SHA256=16e881487f184250deb4fcb60432d7556ab12cb58caea71ef23960aec6c0405a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libsamplerate)
unset(CMAKE_POLICY_VERSION_MINIMUM)
if("${SONICATOR_BUILD_TESTING_SAVED}" STREQUAL "")
    set(BUILD_TESTING ON CACHE BOOL "" FORCE)
else()
    set(BUILD_TESTING ${SONICATOR_BUILD_TESTING_SAVED} CACHE BOOL "" FORCE)
endif()
# The VST3/AU plugin and the tests need the Match renderer and its DSP dependency; a plain
# application build does not compile that code path at all.
option(SONICATOR_BUILD_PLUGIN "Build the VST3/AU plugin" OFF)
set(SONICATOR_ENABLE_MATCH OFF)
if(SONICATOR_BUILD_PLUGIN OR BUILD_TESTING)
    set(SONICATOR_ENABLE_MATCH ON)
endif()
if(SONICATOR_ENABLE_MATCH)
    include(cmake/RubberBand.cmake)
endif()
# libxmp enables verbose stderr tracing through -DDEBUG in Debug builds. Keep the library silent:
# its diagnostics are development noise for an application and would otherwise dominate tests.
if(NOT MSVC)
    target_compile_options(xmp_static PRIVATE $<$<CONFIG:Debug>:-UDEBUG>)
endif()
add_library(SonicatorCore STATIC core/Version.cpp resources/sqlite/sqlite3.c
    core/diagnostics/Diagnostics.cpp
    core/settings/ShortcutPreferences.cpp core/database/Database.cpp core/database/CatalogCompatibility.cpp core/database/PathRelocation.cpp core/database/SearchSyntax.cpp core/database/SampleRepository.cpp core/database/FolderIndex.cpp core/settings/AppSettings.cpp
    core/scanner/FileScanner.cpp core/dsp/BasicAnalyzer.cpp core/dsp/RhythmAnalyzer.cpp core/dsp/AnalysisAudio.cpp core/dsp/TonalAnalyzer.cpp core/dsp/AcousticFeatures.cpp core/dsp/FastAcousticRules.cpp core/dsp/FeatureExtractorV2.cpp core/dsp/FeatureJson.cpp core/audio/AudioEngine.cpp core/audio/MidiSampler.cpp core/audio/SonicatorFormats.cpp core/audio/PreviewReaderSource.cpp core/audio/SampleAudition.cpp core/audio/HoverAudition.cpp core/audio/ContainerValidation.cpp
    core/emu/EblReader.cpp core/emu/EblAudioFormat.cpp
    core/voc/VocAudioFormat.cpp
    core/kurzweil/KrzFile.cpp core/kurzweil/KrzInstrument.cpp
    core/sfz/SfzInstrument.cpp
    core/export/AudioExporter.cpp core/export/SelectionExporter.cpp core/export/ExternalFileDrag.cpp
    core/extract/EmbeddedSamples.cpp core/extract/ExtractService.cpp core/extract/ExtractCandidates.cpp core/extract/ExtractFollowUp.cpp
    core/indexing/IndexingService.cpp core/split/SplitService.cpp core/split/SilenceDetector.cpp
    core/tagging/TaggingService.cpp core/tagging/AutoTagRules.cpp core/tagging/AutoTagService.cpp
    core/smart/SmartTypes.cpp core/smart/KeyNames.cpp core/smart/SmartResolver.cpp core/smart/NameHeuristics.cpp core/smart/WavMetadata.cpp core/smart/AiffMetadata.cpp core/smart/AbletonAsd.cpp core/smart/Rex2Metadata.cpp core/smart/SmartAnalysisService.cpp core/smart/AssetAnalyzer.cpp core/smart/RhythmCandidates.cpp core/smart/TonalCandidates.cpp core/smart/GenreContextExtractor.cpp core/smart/AcousticClassifierService.cpp
    core/sf2/Sf2Bank.cpp core/sf2/Sf2KitSampler.cpp
    core/reason/ReasonPatch.cpp
    core/ableton/AbletonPacks.cpp
    core/sampler/FileInstrument.cpp core/sampler/ExternalKitSampler.cpp
    core/tracker/TrackerFormats.cpp core/tracker/TrackerModReader.cpp core/tracker/TrackerXmReader.cpp
    core/tracker/TrackerS3mReader.cpp core/tracker/TrackerXiReader.cpp
    core/tracker/TrackerDocument.cpp core/tracker/TrackerSongRenderer.cpp
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
    core/rex/Rex2Decoder.cpp core/rex/Rex2SlicePlayer.cpp
    core/export/RiffChunks.cpp
    core/export/DragMetadata.cpp
    core/edit/AudioEditBuffer.cpp
    core/edit/AudioEditMarkers.cpp
    core/edit/AudioEditWriter.cpp
    core/edit/AudioFileWriter.cpp
    core/edit/NormalizeService.cpp
    core/batch/BatchTypes.cpp core/batch/BatchSources.cpp core/batch/BatchPaths.cpp core/batch/BatchPipeline.cpp core/batch/BatchSplit.cpp core/batch/BatchResampler.cpp core/batch/BatchQuantizer.cpp core/batch/BatchMetadata.cpp core/batch/BatchPublisher.cpp core/batch/BatchEncoder.cpp core/batch/BatchService.cpp core/batch/BatchPresetStore.cpp core/batch/BatchPreviewService.cpp
    core/indexing/CatalogFileSync.cpp
    core/contribute/ContributionPayload.cpp core/contribute/ContributionUploader.cpp
    core/updates/UpdateCheck.cpp
    core/ml/MelSpectrogram.cpp core/ml/OnnxAudioClassifier.cpp
    core/smart/TapTempo.cpp
    core/ml/AdaptiveClassifier.cpp core/ml/FeatureV2Backfill.cpp core/ml/GlobalLinearModel.cpp
    core/cloud/CloudSource.cpp core/cloud/CloudLayoutBuilder.cpp core/cloud/CloudCache.cpp core/cloud/CloudBuildService.cpp
    core/match/MatchPolicy.cpp core/match/SourceMetadata.cpp core/match/MatchRenderService.cpp)
target_include_directories(SonicatorCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} resources/sqlite)
target_compile_definitions(SonicatorCore PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    SONICATOR_ENABLE_MATCH=$<BOOL:${SONICATOR_ENABLE_MATCH}>)
target_link_libraries(SonicatorCore PUBLIC
    juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography ZLIB::ZLIB xmp_static onnxruntime_lib
    SampleRate::samplerate lame_static)
if(SONICATOR_ENABLE_MATCH)
    target_sources(SonicatorCore PRIVATE core/match/OfflineMatchRenderer.cpp)
    target_link_libraries(SonicatorCore PUBLIC sonicator_rubberband)
endif()
juce_add_binary_data(SonicatorAssets SOURCES
    resources/branding/logo-silver.png
    resources/fonts/Manrope-Regular.ttf
    resources/fonts/Manrope-Medium.ttf
    resources/fonts/Manrope-SemiBold.ttf
    resources/fonts/Manrope-Bold.ttf
    resources/ml/global_prototypes.json
    resources/ml/global_model.json)
add_library(SonicatorUI STATIC ui/MainComponent.cpp ui/shell/AppFonts.cpp ui/shell/WrappedText.cpp ui/analysis/AnalysisCard.cpp ui/analysis/KeyBpmBlock.cpp ui/library/FileListPanel.cpp ui/library/FolderTree.cpp ui/library/TagStrip.cpp ui/library/TapTempoDialog.cpp ui/waveform/WaveformView.cpp ui/waveform/WaveformDisplay.cpp ui/preview/PreviewPanel.cpp ui/preview/EditCommands.cpp ui/preview/PluginMatchControls.cpp ui/preview/SongProgress.cpp ui/shell/ActionIcons.cpp ui/shell/StudioControls.cpp ui/shell/PanelLayout.cpp ui/shell/PanelSplitter.cpp ui/commands/CommandRegistry.cpp ui/commands/ShortcutBindings.cpp ui/commands/CommandRouter.cpp ui/tagging/AutoTagDialog.cpp ui/split/SplitDialog.cpp ui/extract/ExtractDialog.cpp ui/shell/ThemedDialogs.cpp ui/shell/DialogSurface.cpp ui/settings/SettingsPanel.cpp ui/settings/HotkeysPanel.cpp ui/settings/RelocateFoldersPanel.cpp ui/shell/WindowBounds.cpp ui/disc/DiscBrowserPanel.cpp ui/batch/BatchProcessingDialog.cpp ui/cloud/CloudIndex.cpp ui/cloud/CloudView.cpp)
if(APPLE)
    target_sources(SonicatorUI PRIVATE ui/platform/FileClipboard.mm)
else()
    target_sources(SonicatorUI PRIVATE ui/platform/FileClipboard.cpp)
endif()
target_link_libraries(SonicatorUI PUBLIC SonicatorCore PRIVATE SonicatorAssets)
# Feature extraction for the training stage: off by default, no effect on the application build.
option(SONICATOR_BUILD_TRAINING_TOOLS "Build the training feature extraction tools" OFF)
if(SONICATOR_BUILD_TRAINING_TOOLS)
    add_subdirectory(tools/sonicator-features)
endif()
if(SONICATOR_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
juce_add_gui_app(Sonicator PRODUCT_NAME "SONICATOR" COMPANY_NAME "Sonicator"
    BUNDLE_ID "com.sonicator.browser" MICROPHONE_PERMISSION_ENABLED FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/resources/branding/icon-glossy-chevron-1024.png")
target_sources(Sonicator PRIVATE app/Main.cpp)
target_link_libraries(Sonicator PRIVATE SonicatorUI)
set_target_properties(Sonicator PROPERTIES BUILD_RPATH "${onnxruntime_SOURCE_DIR}/lib")
if(WIN32)
    # BUILD_RPATH has no effect on Windows; the runtime DLL must sit next to the executable.
    add_custom_command(TARGET Sonicator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:Sonicator>"
        VERBATIM)
endif()
if(APPLE)
    # The linker's signature covers only the Mach-O; seal the complete local app bundle.
    # This is an ad-hoc development signature, not Developer ID signing/notarization.
    add_custom_command(TARGET Sonicator POST_BUILD
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignMacBundle.sh"
            "$<TARGET_BUNDLE_DIR:Sonicator>"
        VERBATIM)
endif()
include(CTest)
option(SONICATOR_BUILD_TOOLS "Build development tools (layout report)" OFF)
if(SONICATOR_BUILD_TOOLS)
    add_executable(cloud-layout-report tools/cloud-layout-report/main.cpp)
    target_link_libraries(cloud-layout-report PRIVATE SonicatorCore)
    if(APPLE)
        set_target_properties(cloud-layout-report PROPERTIES BUILD_RPATH "${onnxruntime_SOURCE_DIR}/lib")
    endif()
endif()
if(BUILD_TESTING)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    add_subdirectory(tests)
endif()
