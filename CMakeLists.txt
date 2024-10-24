cmake_minimum_required(VERSION 3.5.0)
project(millicast_viewer VERSION 0.1.0 LANGUAGES C CXX)

include(CTest)

# set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "" FORCE)

enable_testing()


message("${PROJECT_SOURCE_DIR}/lib/millicast/lib/cmake")
#find_package(Cairo)
set(MillicastLIB_DIR "${PROJECT_SOURCE_DIR}/lib/millicast")
set(MillicastSDK_DIR "${MillicastLIB_DIR}/lib/cmake/millicastsdk")
set(CommonSRC_DIR "${MillicastLIB_DIR}/share/millicastsdk/examples/common")
find_package(MillicastSDK REQUIRED)

option( WITH_METADATA "Enable metadata into frame" OFF )

## For subscriber

option( WITH_CV_RENDERER "Render video in a OpenCV" ON )

add_executable(millicast_viewer
    main.cpp
    ${CommonSRC_DIR}/utils.cpp
    ${CommonSRC_DIR}/utils.h
    ${PROJECT_SOURCE_DIR}/3rd/json/nlohmann/json.hpp
    ${PROJECT_SOURCE_DIR}/3rd/argparse/argparse.hpp
)

if( WITH_CV_RENDERER )
    target_compile_definitions( millicast_viewer PRIVATE WITH_CV_RENDERER )
    find_package( OpenCV REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/opencv/lib/cmake")
    find_package( ZeroMQ QUIET HINTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/zeromq/lib/cmake")
    find_package( cppzmq REQUIRED HINTS "${PROJECT_SOURCE_DIR}/lib/cppzmq")
    target_sources(millicast_viewer PRIVATE
        cv_renderer.cpp
        cv_renderer.h
    )

    target_include_directories (millicast_viewer PUBLIC 
        ${OpenCV_INCLUDE_DIRS}
        ${cppzmq_INCLUDE_DIR}
        ${ZeroMQ_INCLUDE_DIR}
        )

    target_link_libraries( millicast_viewer PUBLIC 
        ${OpenCV_LIBS} 
        ${ZeroMQ_LIBRARY})
endif()

target_include_directories (millicast_viewer PUBLIC 
    ${CommonSRC_DIR}
    ${PROJECT_SOURCE_DIR}/3rd/json/
    ${PROJECT_SOURCE_DIR}/3rd/argparse/
    )

target_link_libraries( millicast_viewer PUBLIC Millicast::MillicastSDK )
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


set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
