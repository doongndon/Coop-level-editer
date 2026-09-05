cmake_minimum_required(VERSION 3.21)
project(CoopLevelEditor VERSION 0.1.0)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK 환경변수가 설정되어 있어야 합니다 (Geode CLI로 설치)")
endif()

file(GLOB SOURCES src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

if (NOT EXISTS ${CMAKE_BINARY_DIR}/CPM.cmake)
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
        ${CMAKE_BINARY_DIR}/CPM.cmake
    )
endif()
include(${CMAKE_BINARY_DIR}/CPM.cmake)
CPMAddPackage(
    NAME IXWebSocket
    GITHUB_REPOSITORY machinezone/IXWebSocket
    GIT_TAG v11.4.5
    OPTIONS "USE_ZLIB OFF"
)
target_link_libraries(${PROJECT_NAME} ixwebsocket)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
