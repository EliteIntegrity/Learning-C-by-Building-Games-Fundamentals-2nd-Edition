# Builds every demo in this folder with CMake, on macOS, Linux, or Windows.
# Appendix F of the book, For Mac and Linux Users, explains it.
#
# Every folder here with a main.cpp in it becomes a program, named after
# the folder, with underscores for its spaces, and built into that folder.
# The font is copied beside each program, where it looks for it. A folder
# of your own works the same way: copy the Skeleton folder, give it a name
# of your own, and configure again.

cmake_minimum_required(VERSION 3.24)
project(Fundamentals LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# On Windows, use the copies of SDL that sit beside the demos
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH
        "${CMAKE_CURRENT_SOURCE_DIR}/SDL3"
        "${CMAKE_CURRENT_SOURCE_DIR}/SDL3_ttf")
endif()

# Only what the demos need: no SDL tests, and none of SDL_ttf's extras
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)

# SDL and SDL_ttf: if they're installed already, in new enough versions,
# CMake uses them, and if not, it downloads and builds them, the first time
# only
string(CONCAT sdl_url "https://github.com/libsdl-org/SDL/releases/download/"
    "release-3.4.4/SDL3-3.4.4.tar.gz")
string(CONCAT ttf_url "https://github.com/libsdl-org/SDL_ttf/releases/download/"
    "release-3.2.2/SDL3_ttf-3.2.2.tar.gz")
include(FetchContent)
FetchContent_Declare(SDL3
    URL ${sdl_url}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 3.4 CONFIG)
FetchContent_Declare(SDL3_ttf
    URL ${ttf_url}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    FIND_PACKAGE_ARGS 3.2 CONFIG)
FetchContent_MakeAvailable(SDL3 SDL3_ttf)

# One program for each folder with a main.cpp in it
file(GLOB folders LIST_DIRECTORIES true RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/*")
foreach(folder IN LISTS folders)
    set(dir "${CMAKE_CURRENT_SOURCE_DIR}/${folder}")
    if(NOT EXISTS "${dir}/main.cpp")
        continue()
    endif()
    file(GLOB sources CONFIGURE_DEPENDS "${dir}/*.cpp")
    file(GLOB headers CONFIGURE_DEPENDS "${dir}/*.h")

    # A program's name can't have spaces, so "Vector Lab" is Vector_Lab
    string(REGEX REPLACE "[^A-Za-z0-9_.+-]" "_" target "${folder}")
    add_executable(${target} ${sources} ${headers})
    target_link_libraries(${target} PRIVATE SDL3_ttf::SDL3_ttf SDL3::SDL3)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${dir}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${dir}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${dir}")

    # The font goes beside the program, and on Windows, SDL's DLLs too
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/RobotoMono-Light.ttf" "${dir}")
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${target}> "${dir}"
            COMMAND_EXPAND_LISTS)
    endif()
endforeach()
