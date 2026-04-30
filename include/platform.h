#pragma once

#include <string>

namespace platform {

/// 获取当前可执行文件的绝对路径
std::string get_executable_path();

/// 将路径转换为本机格式（Cygwin POSIX → Windows，其他平台原样返回）
std::string to_native_path(const std::string& path);

/// classpath 分隔符（Windows: ';'，其他: ':'）
char classpath_separator();

/// 动态加载共享库，返回不透明句柄（失败返回 nullptr）
void* load_library(const char* path);

/// 从已加载的库中查找符号
void* get_symbol(void* handle, const char* name);

/// 返回最近一次 load_library 失败的错误描述
std::string load_library_error();

/// 将目录加入共享库搜索路径（仅 Windows 有效，其他平台为空操作）
void add_library_search_dir(const std::string& dir);

/// 在给定的 JAVA_HOME 下查找 JVM 共享库路径，找不到返回空串
std::string find_jvm_library(const std::string& java_home);

} // namespace platform
