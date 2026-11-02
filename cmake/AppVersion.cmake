# App version numbers: v<major>.<minor>.<patch>
#
# <major>.<minor> are set by hand in the VERSION file:
#   major - complete rework or new framework
#   minor - larger changes
# <patch> counts itself: the commits that changed the app's code (app, modules, assets) since
# the VERSION file last changed. Same code, same number.
# Uncommitted changes to that code already show the next number.
#
# Used at configure time (exe metadata) and at build time (the version in the window title and
# the log, see WriteVersion.cmake), so the window is right even before CMake runs again.

function (app_compute_version out_var version_file repo_root git_executable)
    set (code_paths ${ARGN})
    file (STRINGS "${version_file}" major_minor LIMIT_COUNT 1)
    string (STRIP "${major_minor}" major_minor)
    set (patch 0)

    if (git_executable AND EXISTS "${repo_root}/.git")
        file (RELATIVE_PATH version_rel "${repo_root}" "${version_file}")

        # VERSION changed but not committed yet: the new major.minor starts at .0
        execute_process (COMMAND "${git_executable}" diff --quiet HEAD -- "${version_rel}"
                         WORKING_DIRECTORY "${repo_root}" RESULT_VARIABLE version_dirty
                         OUTPUT_QUIET ERROR_QUIET)

        if (version_dirty EQUAL 0)
            execute_process (COMMAND "${git_executable}" log -1 --format=%H -- "${version_rel}"
                             WORKING_DIRECTORY "${repo_root}" OUTPUT_VARIABLE since
                             OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

            # (a new VERSION file that is not committed yet: .0 as well)
            if (since)
                execute_process (COMMAND "${git_executable}" rev-list --count "${since}..HEAD" -- ${code_paths}
                                 WORKING_DIRECTORY "${repo_root}" OUTPUT_VARIABLE patch
                                 OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

                execute_process (COMMAND "${git_executable}" status --porcelain -- ${code_paths}
                                 WORKING_DIRECTORY "${repo_root}" OUTPUT_VARIABLE pending
                                 OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)

                if (NOT patch MATCHES "^[0-9]+$")
                    set (patch 0)
                endif ()

                if (pending)
                    math (EXPR patch "${patch} + 1")
                endif ()
            endif ()
        endif ()
    endif ()

    set (${out_var} "${major_minor}.${patch}" PARENT_SCOPE)
endfunction ()

# The app version for the exe metadata, plus a build step <name>_Version that writes Version.h
# (APP_VERSION_STRING, APP_VERSION_FULL) on every build. Call before juce_add_gui_app, then
# use_app_version on the target. Code paths are relative to the repository root.
function (app_version name out_var)
    set (code_paths ${ARGN})
    set (version_file "${CMAKE_SOURCE_DIR}/VERSION")
    find_package (Git QUIET)

    app_compute_version (version "${version_file}" "${CMAKE_SOURCE_DIR}" "${GIT_EXECUTABLE}" ${code_paths})
    set (${out_var} "${version}" PARENT_SCOPE)

    # CMake runs again after a commit or a VERSION change, for the exe metadata
    set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${version_file}")

    if (EXISTS "${CMAKE_SOURCE_DIR}/.git/logs/HEAD")
        set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/.git/logs/HEAD")
    endif ()

    set (header "${CMAKE_BINARY_DIR}/generated/Version.h")
    string (REPLACE ";" "|" joined_paths "${code_paths}")

    add_custom_target (${name}_Version
        COMMAND "${CMAKE_COMMAND}"
                "-DVERSION_FILE=${version_file}" "-DREPO_ROOT=${CMAKE_SOURCE_DIR}" "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
                "-DCODE_PATHS=${joined_paths}" "-DOUTPUT=${header}"
                -P "${CMAKE_SOURCE_DIR}/cmake/WriteVersion.cmake"
        BYPRODUCTS "${header}"
        COMMENT "Version of ${name}"
        VERBATIM)

    set_target_properties (${name}_Version PROPERTIES FOLDER "Version")
endfunction ()

# A target that compiles the app's sources gets the current Version.h.
function (use_app_version target name)
    add_dependencies (${target} ${name}_Version)
    target_include_directories (${target} PRIVATE "${CMAKE_BINARY_DIR}/generated")
endfunction ()
