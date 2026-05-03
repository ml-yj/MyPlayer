cmake_minimum_required(VERSION 3.26)

project(MyPlayer VERSION 1.0.0 LANGUAGES CXX CUDA)

set(MYPLAYER_EXTERNAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "External dependency root")
get_filename_component(MYPLAYER_EXTERNAL_ROOT "${MYPLAYER_EXTERNAL_ROOT}" ABSOLUTE)

if(NOT WIN32)
    message(FATAL_ERROR "This checkout is Windows-only. Use the separate platform checkout for other builds.")
endif()

list(PREPEND CMAKE_MODULE_PATH "${MYPLAYER_EXTERNAL_ROOT}/cmake")

set(MYPLAYER_CUDA_ARCHITECTURES "52" CACHE STRING "CUDA architectures for cuda_preprocess.cu")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)
set(CMAKE_CUDA_ARCHITECTURES "${MYPLAYER_CUDA_ARCHITECTURES}")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/app/ui")

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

include(MyPlayerQt)
include(MyPlayerDependencies)
include(MyPlayerSources)

add_executable(MyPlayer ${MYPLAYER_SOURCE_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${MYPLAYER_SOURCE_FILES})

target_include_directories(MyPlayer PRIVATE ${MYPLAYER_INCLUDE_DIRS})

target_link_directories(MyPlayer PRIVATE
    ${MYPLAYER_FFMPEG_LINK_DIRS}
    "${MYPLAYER_ONNXRUNTIME_LIB_DIR}"
    "${MYPLAYER_WHISPER_LIB_DIR}"
    "$<$<CONFIG:Debug>:${MYPLAYER_BYTETRACK_LIB_DEBUG_DIR}>"
    "$<$<NOT:$<CONFIG:Debug>>:${MYPLAYER_BYTETRACK_LIB_RELEASE_DIR}>"
    "$<$<CONFIG:Debug>:${MYPLAYER_OPENCV_LIB_DEBUG_DIR}>"
    "$<$<NOT:$<CONFIG:Debug>>:${MYPLAYER_OPENCV_LIB_RELEASE_DIR}>"
    "$<$<CONFIG:Debug>:${MYPLAYER_LIBASS_ROOT}/debug/lib>"
    "$<$<NOT:$<CONFIG:Debug>>:${MYPLAYER_LIBASS_ROOT}/lib>"
)

target_link_libraries(MyPlayer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Sql
    myplayer_libass
    myplayer_bytetrack
    "${MYPLAYER_ONNXRUNTIME_LIBRARY}"
    "${MYPLAYER_WHISPER_LIBRARY}"
    ${MYPLAYER_FFMPEG_LINK_LIBRARIES}
    CUDA::cuda_driver
    CUDA::cudart
    CUDA::nppc
    CUDA::nppicc
    CUDA::nppig
    CUDA::nppidei
)

target_link_libraries(MyPlayer PRIVATE Dbghelp)

if(MSVC)
    target_compile_options(MyPlayer PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:/W3>"
        "$<$<COMPILE_LANGUAGE:CXX>:/sdl>"
        "$<$<COMPILE_LANGUAGE:CXX>:/permissive->"
        "$<$<COMPILE_LANGUAGE:CXX>:/MP>"
    )
    target_link_options(MyPlayer PRIVATE "$<$<CONFIG:Release>:/OPT:REF>" "$<$<CONFIG:Release>:/OPT:ICF>")
    set_property(TARGET MyPlayer PROPERTY VS_GLOBAL_VcpkgApplocalDeps "false")
    set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT MyPlayer)
endif()

set_target_properties(MyPlayer PROPERTIES
    VERSION "${PROJECT_VERSION}"
    RUNTIME_OUTPUT_DIRECTORY "${MYPLAYER_RUNTIME_OUTPUT_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${MYPLAYER_RUNTIME_OUTPUT_DEBUG_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${MYPLAYER_RUNTIME_OUTPUT_RELEASE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${MYPLAYER_RUNTIME_OUTPUT_RELEASE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${MYPLAYER_RUNTIME_OUTPUT_RELEASE_DIR}"
    VS_DEBUGGER_WORKING_DIRECTORY "${MYPLAYER_RUNTIME_OUTPUT_DIR}"
)

include(MyPlayerDeploy)

message(STATUS "MyPlayer external root: ${MYPLAYER_EXTERNAL_ROOT}")
message(STATUS "MyPlayer Qt root: ${MYPLAYER_QT_ROOT}")
message(STATUS "MyPlayer libass root: ${MYPLAYER_LIBASS_ROOT}")
message(STATUS "MyPlayer CUDA include dir: ${MYPLAYER_CUDA_INCLUDE_DIR}")
message(STATUS "MyPlayer runtime output base: ${MYPLAYER_RUNTIME_OUTPUT_DIR}")
message(STATUS "MyPlayer runtime common: ${MYPLAYER_RUNTIME_COMMON_DIR}")
message(STATUS "MyPlayer runtime output Debug: ${MYPLAYER_RUNTIME_OUTPUT_DEBUG_DIR}")
message(STATUS "MyPlayer runtime output Release: ${MYPLAYER_RUNTIME_OUTPUT_RELEASE_DIR}")
