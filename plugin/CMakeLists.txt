# Technical spike: a VST3/AU instrument whose editor hosts the existing MainComponent.
# Built only with -DSONICATOR_BUILD_PLUGIN_SPIKE=ON; the application build is unaffected.
set(spike_target SonicatorPluginSpike)
set(spike_dylib "${onnxruntime_SOURCE_DIR}/lib/libonnxruntime.dylib")
set(spike_sources
    "${CMAKE_CURRENT_SOURCE_DIR}/SpikeProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/SpikeEditor.cpp")

juce_add_plugin(${spike_target}
    PRODUCT_NAME "Sonicator Spike"
    PLUGIN_NAME "Sonicator Spike"
    COMPANY_NAME "Ilya Orange"
    BUNDLE_ID "com.orangeaudio.sonicator.spike"
    PLUGIN_MANUFACTURER_CODE Orng
    PLUGIN_CODE Snsp
    FORMATS VST3 AU
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    # The bundle gets its embedded runtime and its signature after linking, so the manifest
    # helper (which dlopens the built bundle) must run afterwards.
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(${spike_target} PRIVATE ${spike_sources})
target_link_libraries(${spike_target}
    PRIVATE
        SonicatorUI
        juce::juce_audio_processors)
# A VST3-only build must not claim the VST2 parameter IDs; JUCE refuses to compile otherwise.
target_compile_definitions(${spike_target} PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)

if(NOT APPLE)
    return()
endif()

# Opens the editor once against the real catalog copy without a DAW host. It compiles the same
# processor and editor sources as an ordinary application, so no plugin loading is involved.
juce_add_gui_app(${spike_target}_Smoke
    PRODUCT_NAME "Sonicator Spike Smoke"
    COMPANY_NAME "Ilya Orange"
    BUNDLE_ID "com.orangeaudio.sonicator.spike.smoke")
target_sources(${spike_target}_Smoke PRIVATE
    ${spike_sources}
    "${CMAKE_CURRENT_SOURCE_DIR}/SpikeSmoke.cpp")
target_include_directories(${spike_target}_Smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(${spike_target}_Smoke
    PRIVATE
        SonicatorUI
        juce::juce_audio_processors)
target_compile_definitions(${spike_target}_Smoke PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)

# Every wrapper target embeds the ONNX Runtime dylib under Contents/Frameworks and resolves it
# relative to the bundle, so the plugin can be copied out of the build tree.
set(spike_plugin_targets
    ${spike_target}_VST3
    ${spike_target}_AU)

foreach(plugin_target IN LISTS spike_plugin_targets)
    set_target_properties(${plugin_target} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@loader_path/../Frameworks")
    add_custom_command(TARGET ${plugin_target} POST_BUILD
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedDylib.sh"
            "$<TARGET_BUNDLE_DIR:${plugin_target}>"
            "${spike_dylib}"
            "$<TARGET_FILE_NAME:${plugin_target}>"
        VERBATIM)
endforeach()

# Declared after the embed step and before signing: the helper dlopens the built bundle, which
# needs the embedded runtime in place, and the manifest must be covered by the final signature.
juce_enable_vst3_manifest_step(${spike_target})

foreach(plugin_target IN LISTS spike_plugin_targets)
    if(plugin_target MATCHES "VST3")
        set(spike_install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
    else()
        set(spike_install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/Components")
    endif()
    add_custom_command(TARGET ${plugin_target} POST_BUILD
        # The bundle binary changed after linking, so it must be signed last.
        COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignMacPluginBundle.sh"
            "$<TARGET_BUNDLE_DIR:${plugin_target}>"
            "$<TARGET_BUNDLE_CONTENT_DIR:${plugin_target}>/Frameworks/libonnxruntime.dylib"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${spike_install_dir}"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf
            "${spike_install_dir}/$<TARGET_BUNDLE_DIR_NAME:${plugin_target}>"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "$<TARGET_BUNDLE_DIR:${plugin_target}>"
            "${spike_install_dir}/$<TARGET_BUNDLE_DIR_NAME:${plugin_target}>"
        VERBATIM)
endforeach()
