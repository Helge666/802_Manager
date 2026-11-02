juce_add_console_app(import_folder PRODUCT_NAME "import_folder")

target_sources(import_folder PRIVATE main.cpp)

target_compile_definitions(import_folder PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

target_link_libraries(import_folder PRIVATE
    core
    storage_db
    juce::juce_core
    juce::juce_events
)
