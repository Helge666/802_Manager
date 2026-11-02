cmake_minimum_required(VERSION 3.22)

project(802_Manager VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# The C++ runtime goes into the binaries, so nobody needs a Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# The sources are UTF-8 (arrows and symbols in UI strings)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

add_subdirectory(external/JUCE)
include(cmake/AppVersion.cmake)

add_subdirectory(modules/core)
add_subdirectory(modules/midi)
add_subdirectory(modules/storage)

# CLI tools - use the core/storage libraries without the GUI
foreach(tool press_button send_preset_bank send_perform_bank perform_edit send_single_preset
             patch_extract bank_create surprise_me import_folder)
    add_subdirectory(tools/${tool})
endforeach()

juce_add_binary_data(GuiAssets SOURCES
    assets/gui/TX802-Panel-Right.png
    assets/gui/TX802-Panel-Left.png
    assets/gui/LED-On.png
    assets/fonts/NotoSansSymbols2-Regular.ttf
)

# v<major>.<minor> in VERSION, the third number counts the commits that changed the app's code
app_version(802_Manager APP_VERSION app modules assets)

juce_add_gui_app(802_Manager_App
    VERSION ${APP_VERSION}
    PRODUCT_NAME "802_Manager"
    COMPANY_NAME "Helge"
)

target_sources(802_Manager_App PRIVATE
    app/Source/Main.cpp
    app/Source/MainComponent.h
    app/Source/MainComponent.cpp
)

use_app_version(802_Manager_App 802_Manager)

target_compile_definitions(802_Manager_App PRIVATE
    JUCE_WIN_PER_MONITOR_DPI_AWARE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(802_Manager_App
    PRIVATE
        GuiAssets
        juce::juce_gui_basics
        juce::juce_graphics
        juce::juce_core
        juce::juce_events
        juce::juce_data_structures
        juce::juce_dsp
        core
        midi_tx802
        storage_db
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)
