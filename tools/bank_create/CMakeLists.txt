juce_add_console_app(bank_create PRODUCT_NAME "bank_create")

target_sources(bank_create PRIVATE main.cpp)

target_compile_definitions(bank_create PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(bank_create PRIVATE
    core
    storage_db
    juce::juce_core
    juce::juce_events
)
