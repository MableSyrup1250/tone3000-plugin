# Native UI testbed (see plugin/docs/native-ui.md §7). Standalone project so a
# UI iteration never rebuilds the DSP:
#
#   cmake -S plugin/ui/testbed -B build-ui -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build-ui -j
#   build-ui/UiTestbed_artefacts/Debug/UiTestbed.app/Contents/MacOS/UiTestbed --capture out
#
# Also reachable from the plugin build with -DT3K_BUILD_UI_TESTBED=ON, which
# registers `UiTestbed --selftest` with ctest next to the DSP suite.
cmake_minimum_required(VERSION 3.22)
project(UiTestbed VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT TARGET juce::juce_core)
    # Match the versioned checkout used by the plugin's root CMake project.
    set(T3K_JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../libs/juce-9.0.3" CACHE PATH "JUCE source for the standalone testbed")
    add_subdirectory("${T3K_JUCE_SOURCE_DIR}" juce EXCLUDE_FROM_ALL)
endif()

juce_add_gui_app(UiTestbed PRODUCT_NAME "UiTestbed" COMPANY_NAME "TONE3000")

include("${CMAKE_CURRENT_SOURCE_DIR}/../NativeUi.cmake")
t3k_add_native_ui(UiTestbed)
target_include_directories(UiTestbed PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../include")

# The Artemis checkout carries a JUCE keyboard beside the React package.
# Exercise its real focus and button behaviour in the UI testbed when present.
set(_artemis_keyboard_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../../onscreen-keyboard/native")
if (EXISTS "${_artemis_keyboard_dir}/JuceKeyboard.cpp")
    target_sources(UiTestbed PRIVATE "${_artemis_keyboard_dir}/JuceKeyboard.cpp")
    target_include_directories(UiTestbed PRIVATE "${_artemis_keyboard_dir}")
    target_compile_definitions(UiTestbed PRIVATE T3K_TEST_ARTEMIS_KEYBOARD=1)
endif()

target_sources(UiTestbed PRIVATE
    Main.cpp
    Bench.h
    Bench.cpp
    Compare.h
    Compare.cpp
    Drive.h
    Drive.cpp
    Host.h
    Host.cpp
    MockBackend.h
    MockBackend.cpp
    MockSession.h
    MockSession.cpp
    MockSignal.h
    MockSignal.cpp
    ProcessStats.h
    ProcessStats.cpp
    Scenarios.h
    Scenarios.cpp
    SelfTests.h
    SelfTests.cpp
    LayoutTests.cpp
)

target_compile_definitions(UiTestbed PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_WEBP=0
    JUCE_USE_CURL=0
    # Captures pump the message loop synchronously between drive steps.
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:UiTestbed,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="1.0"
    T3K_TESTBED_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(UiTestbed PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Pure-logic unit tests (parsers, wrapping, OAuth, token refresh, the
# connection gate's state machine, tuner math). Captures are not registered:
# they are reviewed by eye or diffed against a previous run, not asserted.
enable_testing()
add_test(NAME UiSelfTests COMMAND UiTestbed --selftest)
