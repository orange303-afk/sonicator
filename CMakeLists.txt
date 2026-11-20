cmake_minimum_required(VERSION 3.22)
# The deployment target has to be set before the first project() call: CMake reads it while it
# initialises the platform, and an assignment afterwards is ignored, which leaves every binary at
# the SDK's own minimum (15.0 on the release runner, which is why the 0.1.1 package refuses older
# systems). 10.15 is the last system the Intel Macs that cannot run Big Sur receive; an arm64 slice
# is promoted to 11.0 by the toolchain because no Apple Silicon Mac runs anything older.
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
project(Sonicator VERSION 0.2.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(SONICATOR_MSVC_COMPILER_JOBS "1" CACHE STRING "MSVC compiler processes per project")
# ONNX Runtime backs the neural sample classifier. The application never loads a model today, yet
# the linked runtime carries a macOS minimum of 13.3 and 52 MB of payload, so a build for older
# systems leaves it out entirely. The tests still exercise it and configure with the option on.
option(SONICATOR_ENABLE_ONNX "Link ONNX Runtime and compile the neural classifier" ON)
if(MSVC AND SONICATOR_MSVC_COMPILER_JOBS GREATER 1)
    # MSBuild /m parallelizes projects, not translation units. Bound compiler parallelism
    # explicitly; release presets serialize projects to avoid multiplying that limit.
    add_compile_options("/MP${SONICATOR_MSVC_COMPILER_JOBS}")
endif()
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
include(FetchContent)
# Extracted archives keep the archive's timestamp instead of the download time. The keyword that
# does this arrived in CMake 3.24; the 3.22 that Ubuntu 22.04 ships (the release baseline) reads it
# as a second URL_HASH value and fails the configure, so the policy is set instead of the keyword.
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
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
# ONNX Runtime publishes its C/C++ binaries as the Microsoft.ML.OnnxRuntime NuGet package, which
# is the install route its documentation lists for C/C++. The GitHub release assets are the same
# binaries but are not a stable host: the repository and its release files answered 404 on
# 2026-11-17. The package is a zip with the headers under build/native/include and one native
# library per runtime identifier; macOS gets a universal dylib by combining its two slices.
if(SONICATOR_ENABLE_ONNX)
    set(ONNXRUNTIME_VERSION 1.20.1)
    set(ONNXRUNTIME_SHA256 9c32108e9a1c78b090a95c2a219f244dd5f067d4caf68fb0f967103f3f20839c)
    set(ONNXRUNTIME_ARCHIVE "${CMAKE_BINARY_DIR}/onnxruntime-${ONNXRUNTIME_VERSION}.nupkg")
    set(ONNXRUNTIME_SOURCE "${CMAKE_BINARY_DIR}/onnxruntime-${ONNXRUNTIME_VERSION}-src")
    if(NOT EXISTS "${ONNXRUNTIME_SOURCE}/.ready")
        if(EXISTS "${ONNXRUNTIME_ARCHIVE}")
            file(SHA256 "${ONNXRUNTIME_ARCHIVE}" onnxruntime_archive_hash)
        endif()
        if(NOT onnxruntime_archive_hash STREQUAL ONNXRUNTIME_SHA256)
            file(DOWNLOAD
                "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime/${ONNXRUNTIME_VERSION}/microsoft.ml.onnxruntime.${ONNXRUNTIME_VERSION}.nupkg"
                "${ONNXRUNTIME_ARCHIVE}"
                EXPECTED_HASH SHA256=${ONNXRUNTIME_SHA256}
                SHOW_PROGRESS)
        endif()
        file(ARCHIVE_EXTRACT INPUT "${ONNXRUNTIME_ARCHIVE}" DESTINATION "${ONNXRUNTIME_SOURCE}")
        if(APPLE)
            file(MAKE_DIRECTORY "${ONNXRUNTIME_SOURCE}/lib")
            execute_process(
                COMMAND /usr/bin/lipo -create
                    "${ONNXRUNTIME_SOURCE}/runtimes/osx-arm64/native/libonnxruntime.dylib"
                    "${ONNXRUNTIME_SOURCE}/runtimes/osx-x64/native/libonnxruntime.dylib"
                    -output "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime.dylib"
                COMMAND_ERROR_IS_FATAL ANY)
            # The package names the library @rpath/libonnxruntime.1.20.1.dylib. Give the combined
            # runtime the unversioned id the app ships under, so build-tree binaries and the bundle
            # resolve the same name; EmbedMacRuntime.sh already rewrites the executable to it.
            execute_process(
                COMMAND /usr/bin/install_name_tool -id "@rpath/libonnxruntime.dylib"
                    "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime.dylib"
                COMMAND_ERROR_IS_FATAL ANY)
        elseif(UNIX)
            file(MAKE_DIRECTORY "${ONNXRUNTIME_SOURCE}/lib")
            file(COPY
                "${ONNXRUNTIME_SOURCE}/runtimes/linux-x64/native/libonnxruntime.so"
                "${ONNXRUNTIME_SOURCE}/runtimes/linux-x64/native/libonnxruntime_providers_shared.so"
                DESTINATION "${ONNXRUNTIME_SOURCE}/lib")
            # The packaged runtime's SONAME is libonnxruntime.so.1, while the package names the file
            # without a version. Build-tree binaries find the directory through their rpath only, so
            # the SONAME has to exist there as well.
            execute_process(COMMAND "${CMAKE_COMMAND}" -E create_symlink libonnxruntime.so
                            "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime.so.1")
        endif()
        file(TOUCH "${ONNXRUNTIME_SOURCE}/.ready")
    endif()
    add_library(onnxruntime_lib SHARED IMPORTED GLOBAL)
    if(APPLE)
        set_target_properties(onnxruntime_lib PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime.dylib"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_SOURCE}/build/native/include"
        )
    elseif(WIN32)
        set_target_properties(onnxruntime_lib PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_SOURCE}/runtimes/win-x64/native/onnxruntime.dll"
            IMPORTED_IMPLIB "${ONNXRUNTIME_SOURCE}/runtimes/win-x64/native/onnxruntime.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_SOURCE}/build/native/include"
        )
    else()
        set_target_properties(onnxruntime_lib PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime.so"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_SOURCE}/build/native/include"
        )
    endif()
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
    # Upstream configMS.h enables HAVE_XMMINTRIN_H on x64, so quantize.c references
    # this intrinsic implementation. POSIX builds keep their configured scalar path.
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
    URL_HASH SHA256=16e881487f184250deb4fcb60432d7556ab12cb58caea71ef23960aec6c0405a)
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
    core/files/FileDisposal.cpp core/files/DuplicateCleanup.cpp
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
    core/contribute/ContributionPayload.cpp core/contribute/ContributionUploader.cpp core/contribute/PlatformName.cpp
    core/updates/UpdateCheck.cpp
    core/ml/MelSpectrogram.cpp
    core/smart/TapTempo.cpp
    core/ml/AdaptiveClassifier.cpp core/ml/FeatureV2Backfill.cpp core/ml/GlobalLinearModel.cpp
    core/cloud/CloudSource.cpp core/cloud/CloudLayoutBuilder.cpp core/cloud/CloudCache.cpp core/cloud/CloudBuildService.cpp
    core/match/MatchPolicy.cpp core/match/SourceMetadata.cpp core/match/MatchRenderService.cpp)
target_include_directories(SonicatorCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} resources/sqlite)
target_compile_definitions(SonicatorCore PUBLIC
    SONICATOR_VERSION="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0
    SONICATOR_ENABLE_MATCH=$<BOOL:${SONICATOR_ENABLE_MATCH}>
    SONICATOR_ENABLE_ONNX=$<BOOL:${SONICATOR_ENABLE_ONNX}>)
target_link_libraries(SonicatorCore PUBLIC
    juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography ZLIB::ZLIB xmp_static
    SampleRate::samplerate lame_static)
if(SONICATOR_ENABLE_ONNX)
    target_sources(SonicatorCore PRIVATE core/ml/OnnxAudioClassifier.cpp)
    target_link_libraries(SonicatorCore PUBLIC onnxruntime_lib)
endif()
if(SONICATOR_ENABLE_MATCH)
    target_sources(SonicatorCore PRIVATE core/match/OfflineMatchRenderer.cpp)
    target_link_libraries(SonicatorCore PUBLIC sonicator_rubberband)
endif()
# Linux audio: ALSA is JUCE's default and always present; JACK is what the Linux audio world expects
# beside it. Both backends are compiled into juce_audio_devices, so the definition has to reach the
# target that compiles the module, and the client library has to be linked explicitly: JUCE's module
# declaration names alsa only.
if(UNIX AND NOT APPLE)
    target_compile_definitions(SonicatorCore PUBLIC JUCE_JACK=1)
    find_library(JACK_LIBRARY jack)
    if(JACK_LIBRARY)
        target_link_libraries(SonicatorCore PUBLIC ${JACK_LIBRARY})
    else()
        message(WARNING "libjack not found: the build will not offer a JACK device type")
    endif()
endif()
juce_add_binary_data(SonicatorAssets SOURCES
    resources/branding/logo-silver.png
    resources/branding/icon-window-256.png
    resources/fonts/Manrope-Regular.ttf
    resources/fonts/Manrope-Medium.ttf
    resources/fonts/Manrope-SemiBold.ttf
    resources/fonts/Manrope-Bold.ttf
    resources/ml/global_prototypes.json
    resources/ml/global_model.json)
add_library(SonicatorUI STATIC ui/MainComponent.cpp ui/shell/AppFonts.cpp ui/shell/AppIcon.cpp ui/shell/WrappedText.cpp ui/analysis/AnalysisCard.cpp ui/analysis/KeyBpmBlock.cpp ui/library/FileListPanel.cpp ui/library/FolderTree.cpp ui/library/TagStrip.cpp ui/library/RoundTagMenu.cpp ui/library/TagsMenuPill.cpp ui/library/TapTempoDialog.cpp ui/waveform/WaveformView.cpp ui/waveform/WaveformDisplay.cpp ui/preview/PreviewPanel.cpp ui/preview/EditCommands.cpp ui/preview/PluginMatchControls.cpp ui/preview/SongProgress.cpp ui/shell/ActionIcons.cpp ui/shell/StudioControls.cpp ui/shell/PanelLayout.cpp ui/shell/PanelSplitter.cpp ui/commands/CommandRegistry.cpp ui/commands/ShortcutBindings.cpp ui/commands/CommandRouter.cpp ui/tagging/AutoTagDialog.cpp ui/split/SplitDialog.cpp ui/extract/ExtractDialog.cpp ui/shell/ThemedDialogs.cpp ui/shell/DialogSurface.cpp ui/settings/SettingsPanel.cpp ui/settings/HotkeysPanel.cpp ui/settings/RelocateFoldersPanel.cpp ui/shell/WindowBounds.cpp ui/disc/DiscBrowserPanel.cpp ui/batch/BatchProcessingDialog.cpp ui/cloud/CloudIndex.cpp ui/cloud/CloudView.cpp ui/library/DuplicateCleanupDialog.cpp)
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
if(APPLE)
    set_target_properties(Sonicator PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@loader_path/../Frameworks")
endif()
if(WIN32 AND SONICATOR_ENABLE_ONNX)
    # BUILD_RPATH has no effect on Windows; the runtime DLL must sit next to the executable.
    add_custom_command(TARGET Sonicator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_SOURCE}/runtimes/win-x64/native/onnxruntime.dll" "$<TARGET_FILE_DIR:Sonicator>"
        VERBATIM)
endif()
if(UNIX AND NOT APPLE AND SONICATOR_ENABLE_ONNX)
    # A packaged Linux build ships the runtime beside the binary; $ORIGIN is the loader's own
    # directory, so nothing points into the build tree. The second entry is where a package keeps
    # its own shared objects.
    set_target_properties(Sonicator PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "$ORIGIN;$ORIGIN/../lib/sonicator")
    # The packaged runtime's SONAME is libonnxruntime.so.1: the loader asks for that name, so the
    # file has to be there under it.
    file(REAL_PATH "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime.so"
         ONNXRUNTIME_LINUX_REAL)
    add_custom_command(TARGET Sonicator POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_LINUX_REAL}" "$<TARGET_FILE_DIR:Sonicator>/libonnxruntime.so.1"
        COMMAND ${CMAKE_COMMAND} -E create_symlink libonnxruntime.so.1
            "$<TARGET_FILE_DIR:Sonicator>/libonnxruntime.so"
        VERBATIM)
    if(EXISTS "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime_providers_shared.so")
        # Loaded lazily by non-CPU execution providers; absent here means a clear library error
        # rather than a silently missing backend.
        add_custom_command(TARGET Sonicator POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime_providers_shared.so"
                "$<TARGET_FILE_DIR:Sonicator>"
            VERBATIM)
    endif()
endif()
if(APPLE)
    if(SONICATOR_ENABLE_ONNX)
        # Ship the runtime inside the app, not at an absolute FetchContent build-tree path.
        # Nested code is signed first; the complete bundle is then sealed ad-hoc.
        add_custom_command(TARGET Sonicator POST_BUILD
            COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedMacRuntime.sh"
                "$<TARGET_BUNDLE_DIR:Sonicator>"
                "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime.dylib"
                "$<TARGET_FILE_NAME:Sonicator>"
            VERBATIM)
    else()
        # Without a bundled runtime there is nothing to embed, but the bundle still has to be
        # signed and sealed before it is packaged.
        add_custom_command(TARGET Sonicator POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:Sonicator>"
            COMMAND /usr/bin/codesign --verify --deep --strict "$<TARGET_BUNDLE_DIR:Sonicator>"
            VERBATIM)
    endif()
endif()
include(CTest)
option(SONICATOR_BUILD_TOOLS "Build development tools (layout report)" OFF)
if(SONICATOR_BUILD_TOOLS)
    add_executable(cloud-layout-report tools/cloud-layout-report/main.cpp)
    target_link_libraries(cloud-layout-report PRIVATE SonicatorCore)
    if(APPLE AND SONICATOR_ENABLE_ONNX)
        set_target_properties(cloud-layout-report PROPERTIES BUILD_RPATH "${ONNXRUNTIME_SOURCE}/lib")
    endif()
endif()
if(BUILD_TESTING)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    add_subdirectory(tests)
endif()
