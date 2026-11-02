# Build step (cmake -P): writes Version.h with the app's current version - only when it changed,
# so an unchanged version recompiles nothing. See AppVersion.cmake.

include ("${CMAKE_CURRENT_LIST_DIR}/AppVersion.cmake")

string (REPLACE "|" ";" code_paths "${CODE_PATHS}")
app_compute_version (version "${VERSION_FILE}" "${REPO_ROOT}" "${GIT_EXECUTABLE}" ${code_paths})

set (content "#pragma once\n#define APP_VERSION_STRING \"${version}\"\n#define APP_VERSION_FULL   \"v${version}\"\n")

if (EXISTS "${OUTPUT}")
    file (READ "${OUTPUT}" existing)
endif ()

if (NOT "${existing}" STREQUAL "${content}")
    file (WRITE "${OUTPUT}" "${content}")
    message (STATUS "App version ${version}")
endif ()
