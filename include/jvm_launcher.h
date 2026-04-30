#pragma once

#include <string>
#include <vector>

struct LaunchConfig {
    std::string java_home;
    std::string classpath;
    std::vector<std::string> lib_dirs;
    std::string main_class;
    std::vector<std::string> jvm_args;
    std::vector<std::string> app_args;
    bool is_jar = false;
};

int parse_args(int argc, char* argv[], LaunchConfig& config);
int launch_jvm(const LaunchConfig& config);
void print_usage(const char* prog);
