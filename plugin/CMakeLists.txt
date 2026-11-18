# The SONICATOR VST3/AU instrument: the shared application window as a DAW editor around the
# application's own catalog. Built only with -DSONICATOR_BUILD_PLUGIN=ON.
set(plugin_target SonicatorPlugin)
if(SONICATOR_ENABLE_ONNX)
    set(plugin_dylib "${ONNXRUNTIME_SOURCE}/lib/libonnxruntime.dylib")
endif()
set(plugin_formats VST3)
if(APPLE)
    list(APPEND plugin_formats AU)
endif()
set(plugin_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/PluginState.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/PluginEditor.cpp")

juce_add_plugin(${plugin_target}
    PRODUCT_NAME "SONICATOR"
    PLUGIN_NAME "SONICATOR"
    COMPANY_NAME "Ilya Orange"
    BUNDLE_ID "com.sonicator.plugin"
    PLUGIN_MANUFACTURER_CODE Orng
    PLUGIN_CODE Sncr
    FORMATS ${plugin_formats}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    # The bundle gets its embedded runtime and its signature after linking, so the manifest
    # helper (which dlopens the built bundle) must run afterwards.
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(${plugin_target} PRIVATE ${plugin_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/PluginFactory.cpp")
target_link_libraries(${plugin_target}
    PRIVATE
        SonicatorUI
        juce::juce_audio_processors)
# A VST3-only build must not claim the VST2 parameter IDs; JUCE refuses to compile otherwise.
target_compile_definitions(${plugin_target} PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)

if(NOT APPLE)
    if(WIN32)
        # The VST3 manifest helper loads this module, so its runtime must exist first.
        if(SONICATOR_ENABLE_ONNX)
            add_custom_command(TARGET ${plugin_target}_VST3 POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${ONNXRUNTIME_SOURCE}/runtimes/win-x64/native/onnxruntime.dll"
                    "$<TARGET_FILE_DIR:${plugin_target}_VST3>"
                VERBATIM)
        endif()
        juce_enable_vst3_manifest_step(${plugin_target})
    endif()
    return()
endif()

# Opens the editor once against the real catalog without a DAW host. It compiles the same
# processor and editor sources as an ordinary application, so no plugin loading is involved.
juce_add_gui_app(${plugin_target}Smoke
    PRODUCT_NAME "SONICATOR Plugin Smoke"
    COMPANY_NAME "Ilya Orange"
    BUNDLE_ID "com.sonicator.plugin.smoke")
target_sources(${plugin_target}Smoke PRIVATE
    ${plugin_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/PluginSmoke.cpp")
target_include_directories(${plugin_target}Smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(${plugin_target}Smoke
    PRIVATE
        SonicatorUI
        juce::juce_audio_processors)
target_compile_definitions(${plugin_target}Smoke PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)

# Every wrapper target embeds the ONNX Runtime dylib under Contents/Frameworks and resolves it
# relative to the bundle, so the plugin can be copied out of the build tree.
set(plugin_wrapper_targets
    ${plugin_target}_VST3
    ${plugin_target}_AU)

foreach(wrapper_target IN LISTS plugin_wrapper_targets)
    set_target_properties(${wrapper_target} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path/../Frameworks")
    if(SONICATOR_ENABLE_ONNX)
        add_custom_command(TARGET ${wrapper_target} POST_BUILD
            COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedDylib.sh"
                "$<TARGET_BUNDLE_DIR:${wrapper_target}>"
                "${plugin_dylib}"
                "$<TARGET_FILE_NAME:${wrapper_target}>"
            VERBATIM)
    endif()
endforeach()

# Declared after the embed step and before signing: the helper dlopens the built bundle, which
# needs the embedded runtime in place, and the manifest must be covered by the final signature.
juce_enable_vst3_manifest_step(${plugin_target})

# Signing the built bundle is part of the build. Copying it into the user's plug-in folders is
# opt-in: a verification build must not replace an installed plug-in behind the user's back.
option(SONICATOR_INSTALL_PLUGIN "Copy built plug-ins into the user's plug-in folders" OFF)
foreach(wrapper_target IN LISTS plugin_wrapper_targets)
    if(wrapper_target MATCHES "VST3")
        set(plugin_install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
    else()
        set(plugin_install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    endif()
    if(SONICATOR_INSTALL_PLUGIN)
        add_custom_command(TARGET ${wrapper_target} POST_BUILD
            # The bundle binary changed after linking, so it must be signed last.
            COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignMacPluginBundle.sh"
                "$<TARGET_BUNDLE_DIR:${wrapper_target}>"
                "$<TARGET_BUNDLE_CONTENT_DIR:${wrapper_target}>/Frameworks/libonnxruntime.dylib"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${plugin_install_dir}"
            COMMAND "${CMAKE_COMMAND}" -E rm -rf
                "${plugin_install_dir}/$<TARGET_BUNDLE_DIR_NAME:${wrapper_target}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "$<TARGET_BUNDLE_DIR:${wrapper_target}>"
                "${plugin_install_dir}/$<TARGET_BUNDLE_DIR_NAME:${wrapper_target}>"
            VERBATIM)
    else()
        add_custom_command(TARGET ${wrapper_target} POST_BUILD
            COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignMacPluginBundle.sh"
                "$<TARGET_BUNDLE_DIR:${wrapper_target}>"
                "$<TARGET_BUNDLE_CONTENT_DIR:${wrapper_target}>/Frameworks/libonnxruntime.dylib"
            VERBATIM)
    endif()
endforeach()
