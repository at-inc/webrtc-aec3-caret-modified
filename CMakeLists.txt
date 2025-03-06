set(CMAKE_VERBOSE_MAKEFILE ON)
cmake_minimum_required(VERSION 3.0)

set(CMAKE_CXX_STANDARD              14)
set(CMAKE_CXX_FLAGS_DEBUG           "-O0")
set(CMAKE_C_FLAGS_DEBUG             "-O0")
set(CMAKE_CXX_FLAGS_DEBUG           "-g")
set(CMAKE_CXX_FLAGS_RELEASE         "-g")
set(CMAKE_CXX_FLAGS_DEBUG           "-gdwarf-2")
set(PROJECT                         CaretAEC)
set(TARGET_NAME                     ${PROJECT})
project(${PROJECT}                  VERSION 1.0.0)

# Enable shared library building
set(BUILD_SHARED_LIBS ON)

# Add definition to enable deprecated features like std::result_of
add_definitions(-D_LIBCPP_ENABLE_CXX17_REMOVED_FEATURES)

add_compile_options(-Wall -fPIC -Wno-deprecated -fexceptions)

# should be set before 'project' statement, otherwise it may not work.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version" FORCE)
    set(DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum OS X deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    # Set RPATH for macOS dylib
    set(CMAKE_MACOSX_RPATH ON)
    set(CMAKE_INSTALL_RPATH "@executable_path/../lib")
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
endif()

if(APPLE)
    add_definitions(-DWEBRTC_MAC -DWEBRTC_POSIX)
elseif(UNIX AND NOT APPLE)
    add_definitions(-DWEBRTC_POSIX)
elseif(WIN32)
    add_definitions(-DWEBRTC_WIN)
endif()

# Patch the Abseil type_traits.h file to fix std::result_of issue
set(ABSEIL_TYPE_TRAITS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/AEC3_cmake/base/abseil/absl/meta/type_traits.h")
if(EXISTS "${ABSEIL_TYPE_TRAITS_PATH}")
    file(READ "${ABSEIL_TYPE_TRAITS_PATH}" TYPE_TRAITS_CONTENT)
    string(REPLACE "using result_of_t = typename std::result_of<T>::type;" 
                   "#if defined(__cpp_lib_is_invocable) && __cpp_lib_is_invocable >= 201703\nusing result_of_t = typename std::invoke_result<T>::type;\n#else\nusing result_of_t = typename std::result_of<T>::type;\n#endif" 
                   MODIFIED_CONTENT "${TYPE_TRAITS_CONTENT}")
    file(WRITE "${ABSEIL_TYPE_TRAITS_PATH}" "${MODIFIED_CONTENT}")
endif()

add_subdirectory(./AEC3_cmake)

# Create a dummy source file
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp "// Dummy source file\n")

# Define framework target
add_library(${PROJECT} SHARED 
    ${CMAKE_CURRENT_BINARY_DIR}/dummy.cpp
)

# Set framework properties
set_target_properties(${PROJECT} PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER at.studio.Caret.audioEngine.AEC
    MACOSX_FRAMEWORK_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING ${PROJECT_VERSION}
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME ${PROJECT}
    XCODE_ATTRIBUTE_INSTALL_PATH "@rpath"
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_DYLIB_INSTALL_NAME_BASE "@rpath"
)

if(APPLE)
    set(LIBRARIES
        "-framework Cocoa"
        "-framework Security"
        "-framework Foundation"
        "-framework ServiceManagement"
    )
elseif(UNIX AND NOT APPLE)
    set(LIBRARIES "")
elseif(WIN32)
    set(LIBRARIES
        -liphlpapi -lwinmm -limm32 -lws2_32 -liphlpapi -lpsapi -lIphlpapi
    )
endif()

target_compile_options(${PROJECT} PRIVATE -Wformat)
target_include_directories(${PROJECT} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/api
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/base/rtc_base
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/base
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/base/system_wrappers/include
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/base/abseil
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/base/jsoncpp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/audio_processing
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/audio_processing/aec3/
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/audio_processing/resampler
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/audio_processing/logging
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/audio_processing/include
    ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/audio_processing/utility
)

# Link all libraries in a single call with consistent keyword usage
target_link_libraries(${PROJECT} PRIVATE
    ${LIBRARIES}
    webrtc_aec3
)

# Create modulemap file
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/module.modulemap 
"framework module CaretAEC {
  umbrella header \"CaretAEC.h\"
  
  export *
  module * { export * }
}
")

# Create umbrella header
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/CaretAEC.h
"#ifndef CARETAEC_H
#define CARETAEC_H

// WebRTC AEC3 API Headers
#include \"webrtc/modules/audio_processing/include/audio_processing.h\"
#include \"webrtc/modules/audio_processing/aec3/echo_canceller3.h\"
#include \"webrtc/modules/audio_processing/aec3/echo_canceller3_config.h\"

// Add any other public headers you want to expose

#endif // CARETAEC_H
")

# Install the framework
install(TARGETS ${PROJECT}
    FRAMEWORK DESTINATION Library/Frameworks
)

# Copy headers to framework's Headers directory
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/webrtc_AEC3/
    DESTINATION Library/Frameworks/${PROJECT}.framework/Headers/webrtc
    FILES_MATCHING PATTERN "*.h"
)

# Install umbrella header
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/CaretAEC.h
    DESTINATION Library/Frameworks/${PROJECT}.framework/Headers
)

# Install modulemap
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/module.modulemap
    DESTINATION Library/Frameworks/${PROJECT}.framework/Modules
)

# Generate and install export headers
include(GenerateExportHeader)
generate_export_header(${PROJECT})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT}_export.h
    DESTINATION Library/Frameworks/${PROJECT}.framework/Headers
)

