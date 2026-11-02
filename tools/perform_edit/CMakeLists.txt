juce_add_console_app(perform_edit PRODUCT_NAME "perform_edit")

target_sources(perform_edit PRIVATE main.cpp)

target_compile_definitions(perform_edit PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(perform_edit PRIVATE
    core
    juce::juce_core
    juce::juce_audio_devices
    juce::juce_events
)
