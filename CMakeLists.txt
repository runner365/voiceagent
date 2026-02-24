cmake_minimum_required(VERSION 3.10)
project(voiceagent)

set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -std=c++17 -g -Wno-deprecated -Wno-deprecated-declarations -Wall -fexceptions -frtti -D__STDC_FORMAT_MACROS -fPIC")


set(CMAKE_OUTPUT_BASE ${CMAKE_BINARY_DIR}/output)
set(BUILD_OUTPUT_BASE ${CMAKE_BINARY_DIR}/output)
set(PREFIX_DIR "${BUILD_OUTPUT_BASE}")
set(INSTALL_RPATH "${PREFIX_DIR}/lib")

# set output static libary
SET(LIBRARY_OUTPUT_PATH ${CMAKE_BINARY_DIR}/output/lib)

# set pkgconfig path
set(CMAKE_PREFIX_PATH "${CMAKE_BINARY_DIR}/output/lib/pkgconfig")

# 添加链接目录
link_directories(${CMAKE_BINARY_DIR}/output/lib
                 /usr/local/lib)

include_directories(${CMAKE_BINARY_DIR}/output/include
                    ${CMAKE_BINARY_DIR}/output/include/openssl
                    ${PROJECT_SOURCE_DIR}/src
                    ${PROJECT_SOURCE_DIR}/src/config
                    ${PROJECT_SOURCE_DIR}/src/utils
                    ${PROJECT_SOURCE_DIR}/src/utils/av
                    ${PROJECT_SOURCE_DIR}/src/net
                    ${PROJECT_SOURCE_DIR}/src/net/http
                    ${PROJECT_SOURCE_DIR}/src/net/tcp
                    ${PROJECT_SOURCE_DIR}/src/net/udp
                    ${PROJECT_SOURCE_DIR}/src/ws_message
                    ${PROJECT_SOURCE_DIR}/src/room
                    ${PROJECT_SOURCE_DIR}/src/transcode
                    ${PROJECT_SOURCE_DIR}/3rdparty/sherpa-onnx
                    ${SRC_INCLUDE_DIRS}
                    /usr/local/include)
# 包含yaml-cpp库
add_subdirectory(3rdparty)

# 收集源文件
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/**/*.cpp"
)

# 收集头文件
file(GLOB_RECURSE HEADERS
    "src/*.hpp"
    "src/*.h"
    "src/**/*.hpp"
    "src/**/*.h"
)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

add_dependencies(${PROJECT_NAME} openssl uv srtp2-ext yaml-cpp libffmpeg sherpa-onnx-cxx-api)
IF (APPLE)
target_link_libraries(${PROJECT_NAME} dl z bz2 iconv 
                    ${PREFIX_DIR}/lib/libopus.a 
                    ${PREFIX_DIR}/lib/libx264.a 
                    ssl crypto srtp2 uv yaml-cpp 
                    avcodec avformat avutil swscale avfilter avdevice swresample
                    sherpa-onnx-cxx-api)
# Link macOS frameworks required by FFmpeg
target_link_libraries(${PROJECT_NAME}
    "-framework AudioToolbox"
    "-framework CoreMedia"
    "-framework CoreFoundation"
    "-framework CoreAudio"
    "-framework VideoToolbox"
    "-framework Security"
    "-framework CoreGraphics"
    "-framework AppKit"
    "-framework CoreVideo"
    "-framework OpenGL"
    "-framework CoreImage"
)
ELSEIF (UNIX)
target_link_libraries(${PROJECT_NAME} rt dl z m bz2 iconv 
                    ${PREFIX_DIR}/lib/libopus.a 
                    ${PREFIX_DIR}/lib/libx264.a 
                    pthread ssl crypto srtp2 uv yaml-cpp 
                    avcodec avformat avutil swscale avfilter avdevice swresample
                    sherpa-onnx-cxx-api)
ENDIF ()

# On macOS, set RPATH so the built executable can find dylibs in build/output/lib at runtime.
# We use @executable_path/output/lib because the executable is at build/ and the libs are at build/output/lib.
if(APPLE)
    # Enable usage of RPATH on macOS
    set(CMAKE_MACOSX_RPATH TRUE)

    # Ensure build and install RPATHs include the runtime relative path to output/lib
    set_target_properties(${PROJECT_NAME} PROPERTIES
        BUILD_RPATH "@executable_path/output/lib"
        INSTALL_RPATH "@executable_path/output/lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()