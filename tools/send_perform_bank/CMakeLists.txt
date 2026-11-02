juce_add_console_app(send_perform_bank PRODUCT_NAME "send_perform_bank")

target_sources(send_perform_bank PRIVATE main.cpp)

target_compile_definitions(send_perform_bank PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(send_perform_bank PRIVATE
    core
    juce::juce_core
    juce::juce_audio_devices
    juce::juce_events
)
