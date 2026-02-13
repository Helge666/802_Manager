add_executable(press_button main.cpp)

target_link_libraries(press_button PRIVATE
    core
    juce::juce_core
    juce::juce_audio_devices
    juce::juce_events
)

# Static link on MinGW
if (MINGW)
    target_link_options(press_button PRIVATE -static-libstdc++ -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()
