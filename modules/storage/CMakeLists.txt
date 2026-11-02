add_library(storage_db)

file(GLOB STORAGE_HEADERS CONFIGURE_DEPENDS include/storage/*.h)
file(GLOB STORAGE_SOURCES CONFIGURE_DEPENDS src/*.cpp)

target_sources(storage_db
    PUBLIC ${STORAGE_HEADERS}
    PRIVATE ${STORAGE_SOURCES}
)

target_include_directories(storage_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(storage_db PUBLIC juce::juce_core)

if (WIN32)
    # Windows has no system SQLite - the amalgamation is compiled in
    set(SQLITE3_AMALGAMATION ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/sqlite/sqlite3.c)
    target_sources(storage_db PRIVATE ${SQLITE3_AMALGAMATION})
    target_include_directories(storage_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/sqlite)
    target_compile_definitions(storage_db PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
else()
    # Use system sqlite3 on non-Windows builds
    target_link_libraries(storage_db PUBLIC sqlite3)
endif()
