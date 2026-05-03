cmake_minimum_required(VERSION 3.26)

project(ByteTrack LANGUAGES CXX)

get_filename_component(BYTETRACK_EXTERNAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

set(BYTETRACK_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/bytetrack" CACHE PATH "ByteTrack public include directory")
set(BYTETRACK_THIRD_PARTY_INCLUDE_DIR "${BYTETRACK_EXTERNAL_ROOT}/include" CACHE PATH "Third-party include root")
set(BYTETRACK_OUTPUT_ROOT "${BYTETRACK_EXTERNAL_ROOT}/lib" CACHE PATH "ByteTrack library output root")

foreach(required_dir IN ITEMS
    "${BYTETRACK_INCLUDE_DIR}"
    "${BYTETRACK_THIRD_PARTY_INCLUDE_DIR}/Eigen"
    "${BYTETRACK_OUTPUT_ROOT}"
)
    if(NOT EXISTS "${required_dir}")
        message(FATAL_ERROR "Required ByteTrack path not found: ${required_dir}")
    endif()
endforeach()

add_library(bytetrack STATIC
    BYTETracker.cpp
    STrack.cpp
    kalmanFilter.cpp
    lapjv.cpp
    "${BYTETRACK_INCLUDE_DIR}/BYTETracker.h"
    "${BYTETRACK_INCLUDE_DIR}/STrack.h"
    "${BYTETRACK_INCLUDE_DIR}/kalmanFilter.h"
    "${BYTETRACK_INCLUDE_DIR}/lapjv.h"
    "${BYTETRACK_INCLUDE_DIR}/dataType.h"
)

target_include_directories(bytetrack
    PUBLIC
        "${BYTETRACK_INCLUDE_DIR}"
        "${BYTETRACK_THIRD_PARTY_INCLUDE_DIR}"
)

target_compile_features(bytetrack PUBLIC cxx_std_17)

target_compile_definitions(bytetrack
    PRIVATE
        _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    target_compile_options(bytetrack PRIVATE /W3 /permissive- /MP)
    set_property(TARGET bytetrack PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    set_property(TARGET bytetrack PROPERTY VS_GLOBAL_VcpkgApplocalDeps "false")
endif()

set(_bytetrack_config_output_dir
    "${BYTETRACK_OUTPUT_ROOT}/$<IF:$<CONFIG:Debug>,Debug,Release>/bytetrack")

set_target_properties(bytetrack PROPERTIES
    OUTPUT_NAME "bytetrack"
    DEBUG_POSTFIX "d"
    ARCHIVE_OUTPUT_DIRECTORY "${_bytetrack_config_output_dir}"
    COMPILE_PDB_OUTPUT_DIRECTORY "${_bytetrack_config_output_dir}"
    COMPILE_PDB_NAME "bytetrack"
    COMPILE_PDB_NAME_DEBUG "bytetrackd"
    FOLDER "third_party"
)

message(STATUS "ByteTrack external root: ${BYTETRACK_EXTERNAL_ROOT}")
message(STATUS "ByteTrack include dir: ${BYTETRACK_INCLUDE_DIR}")
message(STATUS "ByteTrack output root: ${BYTETRACK_OUTPUT_ROOT}")
