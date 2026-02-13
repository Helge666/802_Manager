add_library(midi_tx802)

file(GLOB MIDI_TX802_HEADERS CONFIGURE_DEPENDS include/midi/*.h)
file(GLOB MIDI_TX802_SOURCES CONFIGURE_DEPENDS src/*.cpp)

target_sources(midi_tx802
    PUBLIC
        ${MIDI_TX802_HEADERS}
    PRIVATE
        ${MIDI_TX802_SOURCES}
)

target_include_directories(midi_tx802
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(midi_tx802
    PUBLIC
        core
        juce::juce_core
        juce::juce_audio_devices
)
