juce_add_console_app(send_single_preset PRODUCT_NAME "send_single_preset")

target_sources(send_single_preset PRIVATE main.cpp)

target_compile_definitions(send_single_preset PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(send_single_preset PRIVATE
    core
    storage_db
    juce::juce_core
    juce::juce_audio_devices
    juce::juce_events
)
