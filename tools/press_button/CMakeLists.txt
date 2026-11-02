juce_add_console_app(press_button PRODUCT_NAME "press_button")

target_sources(press_button PRIVATE main.cpp)

target_compile_definitions(press_button PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(press_button PRIVATE
    core
    juce::juce_core
    juce::juce_audio_devices
    juce::juce_events
)
