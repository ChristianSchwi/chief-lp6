cmake_minimum_required(VERSION 3.15)

project(SixChannelLooper VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# JUCE Configuration
# ============================================================================
# Change this path to where YOUR JUCE is installed:
set(JUCE_DIR "C:/JUCE" CACHE PATH "Path to JUCE installation")

# Load JUCE from that directory (THIS LINE IS CRITICAL!)
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

# ============================================================================
# GUI Application
# ============================================================================

# Create the GUI application target
juce_add_gui_app(LooperGUI
    PRODUCT_NAME "6-Channel Looper"
    COMPANY_NAME "LooperDev"
    BUNDLE_ID "com.looperdev.sixchannellooper"
    ICON_BIG ""
    ICON_SMALL "")

# Add source files for GUI app
target_sources(LooperGUI PRIVATE
    Main.cpp
    MainComponent.cpp
    ChannelStripComponent.cpp
    TransportComponent.cpp
    AudioEngine.cpp
    LoopEngine.cpp
    Channel.cpp
    AudioChannel.cpp
    VSTiChannel.cpp
    PluginHostWrapper.cpp
    SongManager.cpp
    ShowManager.cpp
)

# Add compile definitions
target_compile_definitions(LooperGUI PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="6-Channel Looper"
    JUCE_APPLICATION_VERSION_STRING="1.0.0"
)

# Link JUCE modules
target_link_libraries(LooperGUI PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_gui_extra
)

# Set additional compiler warnings
if(MSVC)
    target_compile_options(LooperGUI PRIVATE /W4)
else()
    target_compile_options(LooperGUI PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ============================================================================
# Console Test Application (Phase examples)
# ============================================================================

juce_add_console_app(LooperTest
    PRODUCT_NAME "6-Channel Looper Test")

# Add source files for test app
target_sources(LooperTest PRIVATE
    Phase4Example.cpp
    AudioEngine.cpp
    LoopEngine.cpp
    Channel.cpp
    AudioChannel.cpp
    VSTiChannel.cpp
    PluginHostWrapper.cpp
    SongManager.cpp
    ShowManager.cpp
)

# Add compile definitions
target_compile_definitions(LooperTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="6-Channel Looper Test"
    JUCE_APPLICATION_VERSION_STRING="1.0.0"
)

# Link JUCE modules
target_link_libraries(LooperTest PRIVATE
    juce::juce_audio_devices
    juce::juce_audio_basics
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
)

# Set additional compiler warnings
if(MSVC)
    target_compile_options(LooperTest PRIVATE /W4)
else()
    target_compile_options(LooperTest PRIVATE -Wall -Wextra -Wpedantic)
endif()
