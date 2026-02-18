cmake_minimum_required(VERSION 3.16)
project(802_Manager_Windows VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE as FetchContent
include(FetchContent)

FetchContent_Declare(
  juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 7.0.11
)

FetchContent_MakeAvailable(juce)

add_subdirectory(modules/core)
add_subdirectory(modules/midi)
add_subdirectory(modules/storage)

# CLI tools – use core/storage libraries without midi module or GUI
add_subdirectory(cli/press_button)
add_subdirectory(cli/send_preset_bank)
add_subdirectory(cli/send_perform_bank)
add_subdirectory(cli/perform_edit)
add_subdirectory(cli/send_single_preset)
add_subdirectory(cli/patch_extract)
add_subdirectory(cli/bank_create)
add_subdirectory(cli/surprise_me)
add_subdirectory(cli/import_folder)

juce_add_binary_data(GuiAssets SOURCES
    ../assets/gui/TX802-Panel-Right.png
    ../assets/gui/TX802-Panel-Left.png
    ../assets/gui/LED-On.png
)

juce_add_gui_app(802_Manager_Windows_App
    PRODUCT_NAME "802_Manager"
    COMPANY_NAME "Helge"
)

target_sources(802_Manager_Windows_App PRIVATE
    src/Main.cpp
    src/MainComponent.h
    src/MainComponent.cpp
)

target_link_libraries(802_Manager_Windows_App PRIVATE
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
    $<$<PLATFORM_ID:Linux>:curl>
)

# Enable high DPI and console on Windows
if (WIN32)
    target_compile_definitions(802_Manager_Windows_App PRIVATE JUCE_WIN_PER_MONITOR_DPI_AWARE=1)
    # Optionally prefer static runtime and disable networking if curl not present
    target_compile_definitions(802_Manager_Windows_App PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
endif()

# Prefer static libstdc++/libgcc on MinGW to reduce DLLs
if (MINGW)
    target_link_options(802_Manager_Windows_App PRIVATE -static-libstdc++ -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()

# ── Install rules ────────────────────────────────────────────────
# Usage: cmake --install <build-dir> --prefix ./dist
install(TARGETS 802_Manager_Windows_App RUNTIME DESTINATION bin)

set(CLI_TOOLS
    press_button send_preset_bank send_perform_bank perform_edit
    send_single_preset patch_extract bank_create surprise_me import_folder
)
install(TARGETS ${CLI_TOOLS} RUNTIME DESTINATION bin)
