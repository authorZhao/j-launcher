cmake_minimum_required(VERSION 3.10)
project(j-launcher)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 从环境变量读取 JAVA_HOME
if(DEFINED ENV{JAVA_HOME})
    set(JAVA_HOME $ENV{JAVA_HOME})
else()
    message(FATAL_ERROR "JAVA_HOME environment variable is not set")
endif()

message(STATUS "JAVA_HOME: ${JAVA_HOME}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} (${CMAKE_CXX_COMPILER})")

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${JAVA_HOME}/include
)

# JNI 平台头文件
if(WIN32 OR CYGWIN)
    include_directories(${JAVA_HOME}/include/win32)
elseif(APPLE)
    include_directories(${JAVA_HOME}/include/darwin)
else()
    include_directories(${JAVA_HOME}/include/linux)
endif()

# 根据平台选择对应的 platform 实现
if(WIN32 OR CYGWIN)
    set(PLATFORM_SRC src/platform/win/platform.cpp)
    message(STATUS "Platform: Windows (MSVC/MinGW/Cygwin)")
elseif(APPLE)
    set(PLATFORM_SRC src/platform/mac/platform.cpp)
    message(STATUS "Platform: macOS")
else()
    set(PLATFORM_SRC src/platform/linux/platform.cpp)
    message(STATUS "Platform: Linux")
endif()

add_executable(j-launcher
    src/main.cpp
    src/jvm_launcher.cpp
    ${PLATFORM_SRC}
)

# dlopen 仅在真正的 UNIX 上需要（Cygwin 走 Windows API）
if(UNIX AND NOT CYGWIN)
    target_link_libraries(j-launcher dl)
endif()

# GCC < 9 需要额外链接 stdc++fs
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(j-launcher stdc++fs)
endif()

# Windows 应用图标
if(WIN32 OR CYGWIN)
    target_sources(j-launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.rc)
endif()

if(WIN32)
    set_target_properties(j-launcher PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
