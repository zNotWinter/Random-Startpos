cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(RandomStartPos VERSION 1.0.0)

file(GLOB SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
endif()

# ---------------------------------------------------------------------------
# Workaround: some Geode SDK versions declare CCFileUtilsWin32::init() twice,
# which breaks the Windows build ("class member cannot be redeclared").
# This removes the duplicate "virtual" declaration before the SDK is compiled.
# ---------------------------------------------------------------------------
if (WIN32)
    set(RSP_HEADER "$ENV{GEODE_SDK}/loader/include/Geode/cocos/platform/win32/CCFileUtilsWin32.h")
    if (EXISTS "${RSP_HEADER}")
        file(READ "${RSP_HEADER}" RSP_CONTENT)
        if (RSP_CONTENT MATCHES "virtual bool init\\(\\);" AND RSP_CONTENT MATCHES "\n[ \t]*bool init\\(\\);")
            string(REPLACE "virtual bool init();" "" RSP_CONTENT "${RSP_CONTENT}")
            file(WRITE "${RSP_HEADER}" "${RSP_CONTENT}")
            message(STATUS "Patched duplicate init() in CCFileUtilsWin32.h")
        endif()
    endif()
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

setup_geode_mod(${PROJECT_NAME})
