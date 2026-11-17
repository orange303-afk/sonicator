# Rubber Band Library (GPL-2.0-or-later), used only by the plug-in's offline Match BPM /
# Match Scale renderer. It is linked into SonicatorCore, so the application binary carries the
# static library too, but no application code path calls it.
#
# Upstream has no CMake project. The single-file amalgamation is compiled as one translation unit
# with an explicit configuration: the built-in FFT and resampler are used on every platform, so
# macOS and Windows render the same audio and neither FFTW nor libsamplerate becomes a system
# dependency. Threading is disabled because the renderer is already inside its own worker and
# offline R3 with one thread is deterministic. No CLI, LADSPA, LV2, VAMP or JNI targets are built
# and no homebrew copy is ever picked up.
set(RUBBERBAND_VERSION "3.3.0")
FetchContent_Declare(rubberband
    URL "https://github.com/breakfastquay/rubberband/archive/refs/tags/v${RUBBERBAND_VERSION}.tar.gz"
    URL_HASH SHA256=2bb837fe00932442ca90e185af8a468f7591df0c002b4a9e27a1bced1563ac84)
FetchContent_MakeAvailable(rubberband)

add_library(sonicator_rubberband STATIC
    "${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp")
target_include_directories(sonicator_rubberband PUBLIC
    "${rubberband_SOURCE_DIR}"
    "${rubberband_SOURCE_DIR}/src")
target_compile_definitions(sonicator_rubberband PRIVATE
    ALREADY_CONFIGURED
    USE_BQRESAMPLER
    USE_BUILTIN_FFT
    NO_TIMING
    NO_THREADING
    NO_THREAD_CHECKS)
if(APPLE OR MSVC)
    # sincosf is a GNU libm extension; Apple's libm and MSVC do not provide it.
    target_compile_definitions(sonicator_rubberband PRIVATE LACK_SINCOS)
endif()
if(MSVC)
    target_compile_definitions(sonicator_rubberband PRIVATE _USE_MATH_DEFINES NOMINMAX
        _CRT_SECURE_NO_WARNINGS)
endif()
# Upstream is C++11 and builds without RTTI assumptions; do not let the project's C++20 flags
# leak into a third-party target that expects its own dialect.
target_compile_features(sonicator_rubberband PRIVATE cxx_std_11)
set_target_properties(sonicator_rubberband PROPERTIES
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON)
