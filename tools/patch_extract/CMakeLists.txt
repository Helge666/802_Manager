juce_add_console_app(patch_extract PRODUCT_NAME "patch_extract")

target_sources(patch_extract PRIVATE main.cpp)

target_compile_definitions(patch_extract PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(patch_extract PRIVATE
    core
    storage_db
    juce::juce_core
    juce::juce_events
)
