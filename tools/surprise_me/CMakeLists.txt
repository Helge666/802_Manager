juce_add_console_app(surprise_me PRODUCT_NAME "surprise_me")

target_sources(surprise_me PRIVATE main.cpp)

target_compile_definitions(surprise_me PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(surprise_me PRIVATE
    core
    storage_db
    juce::juce_core
    juce::juce_events
)
