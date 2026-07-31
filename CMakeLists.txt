cmake_minimum_required(VERSION 3.22)

project(RAWDOG VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(JUCE)

juce_add_gui_app(RAWDOG
    PRODUCT_NAME "RAWDOG"
    COMPANY_NAME "rawdog"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Assets/icon.png"
)

target_sources(RAWDOG PRIVATE
    Source/Main.cpp
    Source/LivePreviewWorker.cpp
    Source/MainComponent.cpp
    Source/MainComponentHeaderEditor.cpp
    Source/MainComponentImageIO.cpp
    Source/MainComponentPluginEditor.cpp
    Source/MainMenuModel.cpp
    Source/PluginListModel.cpp
    Source/PluginPresetsStore.cpp
    Source/PluginScanner.cpp
    Source/PluginHost.cpp
    Source/RawCameraConverter.cpp
    Source/RawImage.cpp
    Source/WaveformView.cpp
    Source/ZoomableImageView.cpp
)

if(APPLE)
    target_sources(RAWDOG PRIVATE Source/MacAppearance.mm)
endif()

target_compile_definitions(RAWDOG PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RAWDOG,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RAWDOG,JUCE_VERSION>"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

target_link_libraries(RAWDOG PRIVATE
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    "-framework ImageIO"
)
