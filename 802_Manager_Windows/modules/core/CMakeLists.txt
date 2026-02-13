add_library(core)

file(GLOB CORE_HEADERS CONFIGURE_DEPENDS include/core/*.h)
file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

# Public headers so consumers include <core/...>
target_sources(core
    PUBLIC
        ${CORE_HEADERS}
    PRIVATE
        ${CORE_SOURCES}
)

target_include_directories(core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(core
    PUBLIC
        juce::juce_core
)
