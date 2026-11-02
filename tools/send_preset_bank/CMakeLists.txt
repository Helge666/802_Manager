juce_add_console_app(send_preset_bank PRODUCT_NAME "send_preset_bank")

target_sources(send_preset_bank PRIVATE main.cpp)

target_compile_definitions(send_preset_bank PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(send_preset_bank PRIVATE
    core
    juce::juce_core
    juce::juce_audio_devices
    juce::juce_events
)
