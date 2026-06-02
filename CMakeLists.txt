cmake_minimum_required(VERSION 3.10.0)

project(millicast_viewer VERSION 0.3.1 LANGUAGES C CXX)

include(CTest)

# set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)

enable_testing()

option( WITH_METADATA "Enable metadata into frame" OFF )


################ Millicast ####################
set(MillicastLIB_DIR "${PROJECT_SOURCE_DIR}/lib/millicast")
set(MillicastCommSRC_DIR "${MillicastLIB_DIR}/share/millicastsdk/examples/common")

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message("Building on macOS")
    set(MillicastSDK_DIR "${MillicastLIB_DIR}/lib/cmake/millicastsdk")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message("Building on Linux")
    find_package(MillicastSDK REQUIRED HINTS "${MillicastLIB_DIR}/lib/x86_64-linux-gnu/cmake")
else()
    message(FATAL_ERROR "Unsupported operating system: ${CMAKE_SYSTEM_NAME}")
endif()

find_package(MillicastSDK REQUIRED)

## For subscriber

execute_process(COMMAND echo "${PROJECT_VERSION}" OUTPUT_VARIABLE PROJECT_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)

# Get current date and time
execute_process(COMMAND date "+%Y-%m-%d" OUTPUT_VARIABLE BUILD_DATE OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND date "+%H:%M:%S %Z" OUTPUT_VARIABLE BUILD_TIME OUTPUT_STRIP_TRAILING_WHITESPACE)

# Try to print MillicatSDK version if defined
if(DEFINED MillicastSDK_VERSION)
    message(STATUS "MillicastSDK version: ${MillicastSDK_VERSION}")
    execute_process(COMMAND echo "${MillicastSDK_VERSION}" OUTPUT_VARIABLE MILLICAST_SDK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
elseif(DEFINED MillicastSDK_VERSION_STRING)
    message(STATUS "MillicastSDK version string: ${MillicastSDK_VERSION_STRING}")
    execute_process(COMMAND echo "${MillicastSDK_VERSION_STRING}" OUTPUT_VARIABLE MILLICAST_SDK_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
    message(WARNING "No version information available for MillicastSDK")
endif()
###############################################


################### FFMPEG ####################
# 1. Define your local path
set(FFMPEG_ROOT "${PROJECT_SOURCE_DIR}/lib/ffmpeg")

# 2. Set Include Directories
# Assuming headers are in ../lib/ffmpeg/include
set(FFMPEG_INCLUDE_DIRS "${FFMPEG_ROOT}/include")

# 3. Set Library Paths manually
# This handles the specific .dylib (macOS) or .so files in your local folder

find_library(AVFORMAT_LIB avformat PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
find_library(AVCODEC_LIB  avcodec  PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
find_library(AVUTIL_LIB   avutil   PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
find_library(SWSCALE_LIB  swscale  PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)
find_library(AVDEVICE_LIB  avdevice  PATHS "${FFMPEG_ROOT}/lib" NO_DEFAULT_PATH)

# Path to the version header in your local build
set(VERSION_HEADER "${FFMPEG_ROOT}/include/libavutil/ffversion.h")

if(EXISTS "${VERSION_HEADER}")
    file(STRINGS "${VERSION_HEADER}" FFMPEG_VERSION_LINE REGEX "#define FFMPEG_VERSION")
    string(REGEX MATCH "\"(.*)\"" FFMPEG_VERSION "${FFMPEG_VERSION_LINE}")
    set(FFMPEG_VERSION ${CMAKE_MATCH_1})
    message(STATUS "Manual Parse - FFmpeg Version: ${FFMPEG_VERSION}")
    execute_process(COMMAND echo "${FFMPEG_VERSION}" OUTPUT_VARIABLE FFMPEG_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)
endif()

# message(STATUS "Detected FFmpeg (libavcodec) version: ${AVCODEC_VERSION}")

set(FFMPEG_LIBRARIES ${AVFORMAT_LIB} ${AVCODEC_LIB} ${AVUTIL_LIB} ${SWSCALE_LIB} ${AVDEVICE_LIB})
#################################################



find_package( OpenCV REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/opencv/lib/cmake")
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
execute_process(COMMAND echo "${OpenCV_VERSION}" OUTPUT_VARIABLE OPENCV_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE)

find_package( ZeroMQ QUIET HINTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/zeromq/lib/cmake")
find_package( cppzmq REQUIRED HINTS "${PROJECT_SOURCE_DIR}/lib/cppzmq")

# Configure the header file
configure_file(build_info.h.in build_info.h)

# Include the generated header directory
include_directories(${CMAKE_CURRENT_BINARY_DIR})


# 1. millicast_viewer
add_executable(millicast_viewer
    millicast_viewer.cpp
    ${PROJECT_SOURCE_DIR}/src/program_info.hpp
    ${PROJECT_SOURCE_DIR}/src/program_info.cpp
    ${PROJECT_SOURCE_DIR}/src/millicast_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/millicast_renderer.hpp
    ${PROJECT_SOURCE_DIR}/src/video_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/video_renderer.hpp
    ${MillicastCommSRC_DIR}/utils.cpp
    ${MillicastCommSRC_DIR}/utils.h
    ${PROJECT_SOURCE_DIR}/3rd/json/nlohmann/json.hpp
    ${PROJECT_SOURCE_DIR}/3rd/argparse/argparse.hpp
)


target_include_directories (millicast_viewer PUBLIC 
    ${OpenCV_INCLUDE_DIRS}
    ${cppzmq_INCLUDE_DIR}
    ${ZeroMQ_INCLUDE_DIR}
    ${MillicastCommSRC_DIR}
    ${PROJECT_SOURCE_DIR}/3rd/json/
    ${PROJECT_SOURCE_DIR}/3rd/argparse/
)

target_link_libraries( millicast_viewer PUBLIC 
    ${OpenCV_LIBS} 
    ${ZeroMQ_LIBRARY}
    Millicast::MillicastSDK
)


set_target_properties( millicast_viewer PROPERTIES CXX_STANDARD 17 )

if( WITH_METADATA )
  #target_compile_definitions( millicast_viewer PRIVATE WITH_METADATA )
  target_compile_definitions( millicast_viewer PRIVATE WITH_METADATA )
endif()


if (WIN32)
macro(copy_millicast_deps_to_build _target_name)
    add_custom_command(TARGET ${_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:${_target_name}> $<TARGET_FILE_DIR:${_target_name}>
    COMMAND_EXPAND_LISTS
    )
    set(_sdk_location "")
    add_custom_command(TARGET ${_target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_PROPERTY:Millicast::MillicastSDK,LOCATION>/../ffmpeg.dll" $<TARGET_FILE_DIR:${_target_name}>
    COMMAND_EXPAND_LISTS
    VERBATIM
    )
endmacro()
  
    COPY_MILLICAST_DEPS_TO_BUILD(millicast_viewer)

endif()


# 2. ffmpeg_viewer
add_executable(ffmpeg_viewer 
    ffmpeg_viewer.cpp
    ${PROJECT_SOURCE_DIR}/src/ffmpeg_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/ffmpeg_renderer.hpp
    ${PROJECT_SOURCE_DIR}/src/video_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/video_renderer.hpp
    ${PROJECT_SOURCE_DIR}/3rd/json/nlohmann/json.hpp
    ${PROJECT_SOURCE_DIR}/3rd/argparse/argparse.hpp
    ${PROJECT_SOURCE_DIR}/src/program_info.hpp
    ${PROJECT_SOURCE_DIR}/src/program_info.cpp
)
set_target_properties( ffmpeg_viewer PROPERTIES CXX_STANDARD 17 )

target_include_directories(ffmpeg_viewer PRIVATE 
    ${FFMPEG_INCLUDE_DIRS} 
    ${OpenCV_INCLUDE_DIRS}
    ${cppzmq_INCLUDE_DIR}
    ${ZeroMQ_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/src/
    ${PROJECT_SOURCE_DIR}/3rd/json/
    ${PROJECT_SOURCE_DIR}/3rd/argparse/
)

target_link_libraries(ffmpeg_viewer PUBLIC 
    ${FFMPEG_LIBRARIES} 
    ${ZeroMQ_LIBRARY}
    ${OpenCV_LIBS}
)

if(0)
# 3. millicast_audio
add_executable(millicast_audio 
    millicast_audio.cpp
    ${PROJECT_SOURCE_DIR}/src/millicast_audio_renderer.cpp
    ${PROJECT_SOURCE_DIR}/src/millicast_audio_renderer.hpp
    ${MillicastCommSRC_DIR}/utils.cpp
    ${MillicastCommSRC_DIR}/utils.h
    ${PROJECT_SOURCE_DIR}/3rd/json/nlohmann/json.hpp
    ${PROJECT_SOURCE_DIR}/3rd/argparse/argparse.hpp
    ${PROJECT_SOURCE_DIR}/src/program_info.hpp
    ${PROJECT_SOURCE_DIR}/src/program_info.cpp
)
set_target_properties( millicast_audio PROPERTIES CXX_STANDARD 17 )

target_include_directories(millicast_audio PRIVATE 
    ${cppzmq_INCLUDE_DIR}
    ${ZeroMQ_INCLUDE_DIR}
    ${MillicastCommSRC_DIR}
    ${PROJECT_SOURCE_DIR}/src/
    ${PROJECT_SOURCE_DIR}/3rd/json/
    ${PROJECT_SOURCE_DIR}/3rd/argparse/
)

target_link_libraries(millicast_audio PUBLIC 
    ${ZeroMQ_LIBRARY}
    Millicast::MillicastSDK
)


# 2. ffmpeg_viewer
add_executable(ffmpeg_audio_sample 
    ffmpeg_audio_sample.cpp
    ${PROJECT_SOURCE_DIR}/3rd/json/nlohmann/json.hpp
    ${PROJECT_SOURCE_DIR}/3rd/argparse/argparse.hpp
    ${PROJECT_SOURCE_DIR}/src/program_info.hpp
    ${PROJECT_SOURCE_DIR}/src/program_info.cpp
)
set_target_properties( ffmpeg_audio_sample PROPERTIES CXX_STANDARD 17 )

target_include_directories(ffmpeg_audio_sample PRIVATE 
    ${FFMPEG_INCLUDE_DIRS} 
    ${cppzmq_INCLUDE_DIR}
    ${ZeroMQ_INCLUDE_DIR}
    ${PROJECT_SOURCE_DIR}/src/
    ${PROJECT_SOURCE_DIR}/3rd/json/
    ${PROJECT_SOURCE_DIR}/3rd/argparse/
)

target_link_libraries(ffmpeg_audio_sample PUBLIC 
    ${FFMPEG_LIBRARIES} 
    ${ZeroMQ_LIBRARY}
)


endif(0)

set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)