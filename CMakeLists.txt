cmake_minimum_required(VERSION 3.22)

project(LightIDE
    VERSION 0.2.0
    DESCRIPTION "Lightweight C/C++ IDE, a modern Dev-C++ alternative"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(LIGHTIDE_BUILD_TESTS "Build the automated tests" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Network Widgets Svg)
find_package(QScintilla REQUIRED)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

add_subdirectory(src)

if(LIGHTIDE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
