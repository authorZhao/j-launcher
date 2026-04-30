#include "platform.h"

#include <filesystem>
#include <vector>
#include <dlfcn.h>
#include <mach-o/dyld.h>

namespace fs = std::filesystem;

namespace platform {

std::string get_executable_path() {
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
        return std::string(buf);
    return "(unknown)";
}

std::string to_native_path(const std::string& path) { return path; }

char classpath_separator() { return ':'; }

void* load_library(const char* path) {
    return dlopen(path, RTLD_LAZY);
}

void* get_symbol(void* handle, const char* name) {
    return dlsym(handle, name);
}

std::string load_library_error() {
    const char* err = dlerror();
    return err ? std::string(err) : std::string{};
}

void add_library_search_dir(const std::string&) {}

std::string find_jvm_library(const std::string& java_home) {
    if (java_home.empty()) return {};
    fs::path hp(java_home);

    std::vector<fs::path> candidates = {
        hp / "lib" / "server" / "libjvm.dylib",
        hp / "jre" / "lib" / "server" / "libjvm.dylib",
        hp / "Contents" / "Home" / "lib" / "server" / "libjvm.dylib",
        hp / "Contents" / "Home" / "jre" / "lib" / "server" / "libjvm.dylib",
    };

    for (auto& c : candidates)
        if (fs::exists(c))
            return c.string();
    return {};
}

} // namespace platform
