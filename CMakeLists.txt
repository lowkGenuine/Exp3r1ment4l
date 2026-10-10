cmake_minimum_required(VERSION 3.22)
project(Exp3r1ment4l VERSION 0.1.4 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
option(EXP3_BUILD_PLUGIN "Build JUCE VST3 plug-ins" ON)
option(EXP3_BUILD_CHECKS "Build host and UI checks" OFF)
if(EXP3_BUILD_CHECKS)
  enable_testing()
  add_executable(Exp3r1ment4lDSPTest Tests/dsp_test.cpp)
  target_include_directories(Exp3r1ment4lDSPTest PRIVATE Source)
  add_test(NAME DSP COMMAND Exp3r1ment4lDSPTest)
endif()
if(NOT EXP3_BUILD_PLUGIN)
  return()
endif()
if(DEFINED JUCE_PATH)
  add_subdirectory("${JUCE_PATH}" JUCE)
else()
  include(FetchContent)
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 4f43011b96eb0636104cb3e433894cda98243626 GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(juce)
endif()
function(exp3_plugin target product code synth)
  if(synth)
    set(categories Instrument Synth)
  else()
    set(categories Fx)
  endif()
  juce_add_plugin(${target}
    COMPANY_NAME "Shaper Audio"
    BUNDLE_ID "audio.shaper.${target}"
    PLUGIN_MANUFACTURER_CODE LkGn PLUGIN_CODE ${code}
    FORMATS VST3 Standalone PRODUCT_NAME "${product}"
    IS_SYNTH ${synth} NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE VST3_CATEGORIES ${categories}
    VST3_AUTO_MANIFEST FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Exp3r1ment4l processes audio input."
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(${target} PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
  target_compile_definitions(${target} PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
  if(MINGW)
    target_link_options(${target} PUBLIC -static)
  endif()
  if(EXP3_BUILD_CHECKS)
    target_compile_definitions(${target} PUBLIC JUCE_MODAL_LOOPS_PERMITTED=1)
  endif()
  target_link_libraries(${target} PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
endfunction()
exp3_plugin(Exp3r1ment4l "Exp3r1ment4l" E3fx FALSE)
exp3_plugin(Exp3r1ment4lInstrument "Exp3r1ment4l Synth" E3sy TRUE)
if(EXP3_BUILD_CHECKS)
  add_executable(Exp3r1ment4lHostCheck Tests/host_check.cpp)
  target_compile_definitions(Exp3r1ment4lHostCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_VST=0 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(Exp3r1ment4lHostCheck PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
  add_executable(Exp3r1ment4lPreview Tests/preview.cpp)
  target_include_directories(Exp3r1ment4lPreview PRIVATE Source
    $<TARGET_PROPERTY:Exp3r1ment4l,INCLUDE_DIRECTORIES>)
  target_compile_definitions(Exp3r1ment4lPreview PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1
    $<TARGET_PROPERTY:Exp3r1ment4l,COMPILE_DEFINITIONS>)
  target_link_libraries(Exp3r1ment4lPreview PRIVATE Exp3r1ment4l)
endif()
