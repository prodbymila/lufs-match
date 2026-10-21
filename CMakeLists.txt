cmake_minimum_required(VERSION 3.22)

# Universal binary (Apple Silicon + Intel/Rosetta hosts); must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(LUFSMatch VERSION 1.0.0)

add_subdirectory(JUCE)

# Local builds copy the plugin here; CI turns copying off and packages the build output.
option(LM_COPY_PLUGIN "Copy the plugin into LM_PLUGIN_DIR after building" ${APPLE})
set(LM_PLUGIN_DIR "/Volumes/X9 Pro/Library/Audio/Plug-Ins" CACHE PATH "Plug-Ins folder the build copies into")

juce_add_plugin(LUFSMatch
    PRODUCT_NAME "LUFS Match"
    COMPANY_NAME "LM"
    BUNDLE_ID com.lm.lufsmatch
    PLUGIN_MANUFACTURER_CODE LmLm
    PLUGIN_CODE LmAt
    FORMATS VST3
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Tools
    COPY_PLUGIN_AFTER_BUILD ${LM_COPY_PLUGIN}
    VST3_COPY_DIR "${LM_PLUGIN_DIR}/VST3")

target_sources(LUFSMatch PRIVATE
    Source/LoudnessEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(LUFSMatch PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(LUFSMatch
    PRIVATE juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

option(LM_BUILD_TESTS "Build the tests" ON)
if(LM_BUILD_TESTS)
    enable_testing()

    # DSP only, no JUCE.
    add_executable(LMEngineTest tests/engine_test.cpp Source/LoudnessEngine.cpp)
    target_include_directories(LMEngineTest PRIVATE Source)
    target_compile_features(LMEngineTest PRIVATE cxx_std_17)
    add_test(NAME engine COMMAND LMEngineTest)

    # Loads the built VST3 twice (Tap + Match) and checks the matching end to end.
    juce_add_console_app(LMHostTest PRODUCT_NAME "LMHostTest")
    target_sources(LMHostTest PRIVATE tests/host_test.cpp Source/LoudnessEngine.cpp)
    target_include_directories(LMHostTest PRIVATE Source)
    target_compile_definitions(LMHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(LMHostTest PRIVATE juce::juce_audio_processors
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(APPLE)
        target_compile_definitions(LMHostTest PRIVATE JUCE_PLUGINHOST_AU=1)
        target_link_libraries(LMHostTest PRIVATE "-framework CoreAudioKit")
    endif()
    add_dependencies(LMHostTest LUFSMatch_VST3)
    add_test(NAME host_vst3 COMMAND LMHostTest
        "${CMAKE_BINARY_DIR}/LUFSMatch_artefacts/$<CONFIG>/VST3/LUFS Match.vst3")

    # Renders the editor to PNG, to check the look without opening a host.
    juce_add_console_app(LMSnapshot PRODUCT_NAME "LMSnapshot")
    target_sources(LMSnapshot PRIVATE tests/snapshot.cpp
        Source/LoudnessEngine.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_include_directories(LMSnapshot PRIVATE Source)
    target_compile_definitions(LMSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(LMSnapshot PRIVATE juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)
endif()
