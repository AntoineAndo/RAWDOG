cmake_minimum_required(VERSION 3.22)

project(PixelBender VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_gui_app(PixelBender
    PRODUCT_NAME "Pixel Bender"
    COMPANY_NAME "pixel-bender"
    ICON_BIG ""
)

target_sources(PixelBender PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponentImageIO.cpp
    Source/MainComponentPluginEditor.cpp
    Source/MainMenuModel.cpp
    Source/PluginListModel.cpp
    Source/PluginScanner.cpp
    Source/PluginHost.cpp
    Source/RawImage.cpp
    Source/WaveformView.cpp
    Source/ZoomableImageView.cpp
)

target_compile_definitions(PixelBender PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PixelBender,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PixelBender,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

target_link_libraries(PixelBender PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
