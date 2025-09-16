cmake_minimum_required(VERSION 3.18)

project(Halo VERSION 3.5.6)
set(TARGET_NAME "halo")


set(POLARIS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../PolarlightBuild")

add_definitions(-DHAS_QOS_FLOWID=1)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
set(HALO_SOURCE_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src_assets")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${POLARIS_ROOT}/out/build/x64-${CMAKE_BUILD_TYPE}/bin)

add_subdirectory(third-party/moonlight-common-c/enet)
target_compile_definitions(enet PRIVATE
    HAS_PQOS_FLOWID=1
    HAS_QOS_FLOWID=1)
set(Boost_USE_STATIC_LIBS ON)
add_subdirectory(third-party/Simple-Web-Server)

set(UPNPC_BUILD_SHARED OFF CACHE BOOL "no shared libraries")
set(UPNPC_BUILD_TESTS OFF CACHE BOOL "Don't build tests for miniupnpc")
set(UPNPC_BUILD_SAMPLE OFF CACHE BOOL "Don't build samples for miniupnpc")
set(UPNPC_NO_INSTALL ON CACHE BOOL "Don't install any libraries build for miniupnpc")
add_subdirectory(third-party/miniupnp/miniupnpc)
include_directories(third-party/miniupnp/miniupnpc/include)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)

find_package(Boost COMPONENTS locale log filesystem program_options REQUIRED)

list(APPEND HALO_COMPILE_OPTIONS -Wall -Wno-missing-braces -Wno-maybe-uninitialized -Wno-sign-compare)

include_directories(${POLARIS_ROOT}/Polarlight-HBridge)

enable_language(RC)
set(CMAKE_RC_COMPILER windres)
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

add_definitions(-DCURL_STATICLIB)
include_directories(${CURL_STATIC_INCLUDE_DIRS})
link_directories(${CURL_STATIC_LIBRARY_DIRS})

add_compile_definitions(HALO_PLATFORM="windows")
include_directories(third-party/ViGEmClient/include)

if(NOT DEFINED HALO_ICON_PATH)
    set(HALO_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/sunshine.ico")
endif()

configure_file(src/platform/windows/windows.rs.in windows.rc @ONLY)
set(PLATFORM_TARGET_FILES
        "${CMAKE_CURRENT_BINARY_DIR}/windows.rc"
        src/platform/windows/misc.h
        src/platform/windows/misc.cpp
        src/platform/windows/input.cpp
        third-party/tray/tray_windows.c
        third-party/ViGEmClient/src/ViGEmClient.cpp
        third-party/ViGEmClient/include/ViGEm/Client.h
        third-party/ViGEmClient/include/ViGEm/Common.h
        third-party/ViGEmClient/include/ViGEm/Util.h
        third-party/ViGEmClient/include/ViGEm/km/BusShared.h)

set(OPENSSL_LIBRARIES
        libssl.a
        libcrypto.a)

list(PREPEND PLATFORM_LIBRARIES
        libstdc++.a
        libwinpthread.a
        libssp.a
        ksuser
        wsock32
        ws2_32
        d3d11 dxgi D3DCompiler
        setupapi
        dwmapi
        userenv
        synchronization.lib
        ${CURL_STATIC_LIBRARIES})

set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp
        PROPERTIES COMPILE_DEFINITIONS "UNICODE=1;ERROR_INVALID_DEVICE_OBJECT_PARAMETER=650")
set_source_files_properties(third-party/ViGEmClient/src/ViGEmClient.cpp
        PROPERTIES COMPILE_FLAGS "-Wno-unknown-pragmas -Wno-misleading-indentation -Wno-class-memaccess")

configure_file(src/version.h.in version.h @ONLY)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(HALO_TARGET_FILES
        third-party/nanors/rs.c
        third-party/nanors/rs.h
        third-party/moonlight-common-c/src/Input.h
        third-party/moonlight-common-c/src/Rtsp.h
        third-party/moonlight-common-c/src/RtspParser.c
        third-party/tray/tray.h
        src/upnp.cpp
        src/upnp.h
        src/utility.h
        src/uuid.h
        src/config.h
        src/config.cpp
        src/entry.cpp
        src/main.h
        src/crypto.cpp
        src/crypto.h
        src/process.cpp
        src/process.h
        src/nvhttp.cpp
        src/nvhttp.h
        src/httpcommon.cpp
        src/httpcommon.h
        src/confighttp.cpp
        src/confighttp.h
        src/input.cpp
        src/input.h
        src/platform/common.h
        src/network.cpp
        src/network.h
        src/move_by_copy.h
        src/system_tray.cpp
        src/system_tray.h
        src/task_pool.h
        src/thread_pool.h
        src/thread_safe.h
        src/sync.h
        src/round_robin.h
        ${PLATFORM_TARGET_FILES})

set_source_files_properties(src/upnp.cpp PROPERTIES COMPILE_FLAGS -Wno-pedantic)

set_source_files_properties(third-party/nanors/rs.c
        PROPERTIES COMPILE_FLAGS "-include deps/obl/autoshim.h -ftree-vectorize")

list(APPEND HALO_DEFINITIONS HALO_TRAY=0)

include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/moonlight-common-c/enet/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/nanors
        ${CMAKE_CURRENT_SOURCE_DIR}/third-party/nanors/deps/obl
        ${PLATFORM_INCLUDE_DIRS}
)

string(TOUPPER "x${CMAKE_BUILD_TYPE}" BUILD_TYPE)
if("${BUILD_TYPE}" STREQUAL "XDEBUG")
        if(WIN32)
                set_source_files_properties(src/nvhttp.cpp PROPERTIES COMPILE_FLAGS -O2)
        endif()
else()
    add_definitions(-DNDEBUG)
endif()

set(HALO_ASSETS_DIR "assets")

list(APPEND HALO_DEFINITIONS HALO_ASSETS_DIR="${HALO_ASSETS_DIR}")

list(APPEND HALO_EXTERNAL_LIBRARIES
        libminiupnpc-static
        ${CMAKE_THREAD_LIBS_INIT}
        enet
        opus
        ${Boost_LIBRARIES}
        ${OPENSSL_LIBRARIES}
        ${CURL_LIBRARIES}
        ${PLATFORM_LIBRARIES})
add_library(${TARGET_NAME} SHARED ${HALO_TARGET_FILES})
#add_executable(${TARGET_NAME} ${HALO_TARGET_FILES})

set_target_properties(${TARGET_NAME} PROPERTIES LINK_SEARCH_START_STATIC 1)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -static")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static-libgcc -static")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".dll")
find_library(ZLIB ZLIB1)

target_link_libraries(${TARGET_NAME} ${HALO_EXTERNAL_LIBRARIES} ${EXTRA_LIBS})
target_compile_definitions(${TARGET_NAME} PUBLIC ${HALO_DEFINITIONS})
set_target_properties(${TARGET_NAME} PROPERTIES CXX_STANDARD 17
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

