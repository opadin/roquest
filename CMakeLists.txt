cmake_minimum_required(VERSION 3.5)

project(roquest LANGUAGES C)

set(CMAKE_C_FLAGS -v)
set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(PROJ_SRC
    ${PROJECT_SOURCE_DIR}/src
)

file(GLOB PROJECT_SOURCES CONFIGURE_DEPENDS
    ${PROJ_SRC}/*.c
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}\\output")
set(EXECUTABLE_OUTPUT_PATH "${PROJECT_SOURCE_DIR}\\output")

INCLUDE(FindPkgConfig)
pkg_check_modules(SDL2 REQUIRED sdl2)

include_directories(
    ${SDL2_INCLUDE_DIRS}
)

link_directories (
    ${SDL2_LIBRARY_DIRS}
)

# add the executable
add_executable(roquest
    ${PROJECT_SOURCES}
)

add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/res ${EXECUTABLE_OUTPUT_PATH}/res
)
add_dependencies(roquest copy_assets)

target_link_libraries (roquest 
    ${SDL2_LIBRARIES}
)

set_target_properties(
    roquest
    PROPERTIES
        OUTPUT_NAME "roquest"
        SUFFIX ".exe"
)
