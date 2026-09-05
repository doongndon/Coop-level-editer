cmake_minimum_required(VERSION 3.21)
project(CoopLevelEditor VERSION 0.1.0)

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK 환경변수가 설정되어 있어야 합니다 (Geode CLI로 설치)")
endif()

# CPM: 로컬 캐시 무시하고 항상 새로 다운로드
set(CPM_USE_LOCAL_PACKAGES OFF)

if (NOT EXISTS ${CMAKE_BINARY_DIR}/CPM.cmake)
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
        ${CMAKE_BINARY_DIR}/CPM.cmake
    )
endif()
include(${CMAKE_BINARY_DIR}/CPM.cmake)

# --- 인증서 묶음 ---
# IXWebSocket은 안드로이드에서 시스템 인증서를 읽지 못한다(구현이 비어 있다).
# 그래서 신뢰할 인증서 묶음을 모드 안에 직접 새겨 넣는다.
#
# 빌드할 때 내려받지 않고 저장소에 넣어두는 이유는, 빌드가 외부 사이트 상태에
# 좌우되지 않게 하고 언제 빌드해도 같은 결과가 나오게 하기 위해서다.
# 갱신이 필요하면 resources/cacert.pem 을 새 파일로 바꾸면 된다.
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/resources/cacert.pem COOP_CA_BUNDLE)
configure_file(cmake/CaBundle.cpp.in ${CMAKE_BINARY_DIR}/generated/CaBundle.cpp @ONLY)

file(GLOB SOURCES src/*.cpp)
list(APPEND SOURCES ${CMAKE_BINARY_DIR}/generated/CaBundle.cpp)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

# --- 암호화 라이브러리 ---
# v3.6 이상은 별도 서브모듈을 요구해서 CPM으로 받으면 설정 단계에서 실패한다.
# v3.5.2는 서브모듈이 없어 그대로 빌드된다.
CPMAddPackage(
    NAME mbedtls
    GITHUB_REPOSITORY Mbed-TLS/mbedtls
    GIT_TAG v3.5.2
    OPTIONS
        "ENABLE_PROGRAMS OFF"
        "ENABLE_TESTING OFF"
        "MBEDTLS_FATAL_WARNINGS OFF"
        "DISABLE_PACKAGE_CONFIG_AND_INSTALL ON"
)

# 윈도우에서는 하드웨어 AES 가속 코드가 컴파일되지 않아 꺼야 한다(자세한 이유는 설정 파일에).
# 설정이 라이브러리마다 달라지면 안 되므로 mbedTLS 세 갈래 모두에 똑같이 적용한다.
if (WIN32)
    foreach (COOP_MBEDTLS_TARGET mbedcrypto mbedx509 mbedtls)
        target_compile_definitions(${COOP_MBEDTLS_TARGET} PUBLIC
            "MBEDTLS_USER_CONFIG_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/mbedtls_user_config.h\""
        )
    endforeach()
endif()

# IXWebSocket은 find_package(MbedTLS)로 라이브러리를 찾으려 한다.
# 우리는 위에서 직접 빌드했으므로 결과를 알려주고 그 단계를 건너뛰게 한다.
set(MBEDTLS_FOUND TRUE)
set(MBEDTLS_INCLUDE_DIRS "${mbedtls_SOURCE_DIR}/include")
set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)

# IXWebSocket v11.4.6 (실제로 존재하는 태그, CMake 3.21 호환)
# IXWEBSOCKET_INSTALL을 끄지 않으면 설치 규칙이 mbedtls 타겟을 내보내려다 실패한다.
CPMAddPackage(
    NAME IXWebSocket
    GITHUB_REPOSITORY machinezone/IXWebSocket
    GIT_TAG v11.4.6
    OPTIONS
        "USE_ZLIB OFF"
        "USE_TLS ON"
        "USE_MBED_TLS ON"
        "IXWEBSOCKET_INSTALL OFF"
)

# MBEDTLS_FOUND를 직접 설정하면 IXWebSocket이 버전 판별 구간을 건너뛴다.
# mbedTLS 3.x는 일부 함수의 인자가 달라서 이 정의를 직접 넣어줘야 컴파일된다.
target_compile_definitions(ixwebsocket PRIVATE IXWEBSOCKET_USE_MBED_TLS_MIN_VERSION_3)

target_link_libraries(${PROJECT_NAME} ixwebsocket)

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
