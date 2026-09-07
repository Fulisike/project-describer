cmake_minimum_required(VERSION 3.10)
project(project-describer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加头文件搜索路径
# 1. 源码根目录（用于 #include "core/scanner.hpp" 等）
# 2. 第三方库目录（用于 #include "cxxopts.hpp"）
include_directories(src)
include_directories(third_party/cxxopts)

# 收集所有源文件（自动包含 src 下所有子目录的 .cpp）
file(GLOB_RECURSE SOURCES "src/*.cpp")

# 生成可执行文件
add_executable(project-describer ${SOURCES})

# MSVC 特定设置：强制使用 UTF-8 编码，禁用 Windows UNICODE 避免路径乱码
if (MSVC)
    target_compile_options(project-describer PRIVATE /utf-8)
    target_compile_definitions(project-describer PRIVATE CXXOPTS_NO_UNICODE)
endif()

# Linux 下可能需要链接 pthread（用于 std::thread，虽然当前未使用，但保留）
if (UNIX)
    target_link_libraries(project-describer pthread)
endif()