#include "jvm_launcher.h"
#include "platform.h"

#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include <jni.h>

namespace fs = std::filesystem;
using namespace platform;

// ============================================================
// Big-endian readers for class file parsing
// ============================================================

static uint16_t be_u2(std::ifstream& f) {
    unsigned char b[2];
    f.read(reinterpret_cast<char*>(b), 2);
    return (uint16_t(b[0]) << 8) | b[1];
}

static uint32_t be_u4(std::ifstream& f) {
    unsigned char b[4];
    f.read(reinterpret_cast<char*>(b), 4);
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
}

// ============================================================
// Parse .class file -> internal class name (e.g. "com/example/Main")
// ============================================================

static std::string read_class_name(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};

    if (be_u4(f) != 0xCAFEBABE) return {};
    f.seekg(4, std::ios::cur); // minor + major version

    uint16_t cp_count = be_u2(f);

    std::vector<std::string> utf8(cp_count);
    std::vector<uint16_t> class_name_idx(cp_count, 0);

    for (uint16_t i = 1; i < cp_count; i++) {
        uint8_t tag;
        f.read(reinterpret_cast<char*>(&tag), 1);
        switch (tag) {
            case 1: { // Utf8
                uint16_t len = be_u2(f);
                utf8[i].resize(len);
                f.read(&utf8[i][0], len);
                break;
            }
            case 3: case 4:  f.seekg(4, std::ios::cur); break;
            case 5: case 6:  f.seekg(8, std::ios::cur); i++; break;
            case 7:  class_name_idx[i] = be_u2(f); break;
            case 8:  f.seekg(2, std::ios::cur); break;
            case 9: case 10: case 11: case 12:
                f.seekg(4, std::ios::cur); break;
            case 15: f.seekg(3, std::ios::cur); break;
            case 16: f.seekg(2, std::ios::cur); break;
            case 17: case 18: f.seekg(4, std::ios::cur); break;
            case 19: case 20: f.seekg(2, std::ios::cur); break;
            default: return {};
        }
    }

    f.seekg(2, std::ios::cur); // access_flags
    uint16_t this_class = be_u2(f);
    if (this_class >= cp_count) return {};

    uint16_t ni = class_name_idx[this_class];
    if (ni == 0 || ni >= cp_count) return {};
    return utf8[ni];
}

// ============================================================
// Helpers
// ============================================================

static std::string get_java_home(const std::string& user_home) {
    if (!user_home.empty()) return user_home;
    if (const char* env = std::getenv("JAVA_HOME"); env && *env) return env;
    return {};
}

static std::string to_forward_slash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

static bool ends_with_ci(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    auto it = str.rbegin();
    auto sit = suffix.rbegin();
    while (sit != suffix.rend()) {
        if (std::tolower(static_cast<unsigned char>(*it)) !=
            std::tolower(static_cast<unsigned char>(*sit)))
            return false;
        ++it; ++sit;
    }
    return true;
}

// ============================================================
// Usage / arg parsing
// ============================================================

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [options] <class|jar|class-file> [args...]\n\n"
        << "Options:\n"
        << "  -cp, -classpath <path>   Class search path\n"
        << "  -lib <dir>               Add all jar files in <dir> to classpath (repeatable)\n"
        << "  --java-home <path>       Override JAVA_HOME\n"
        << "  -J<flag>                 Pass flag to JVM (e.g. -J-Xmx512m)\n"
        << "  -h, --help               Show this help\n\n"
        << "Targets:\n"
        << "  <class-name>   Fully qualified class (e.g. com.example.Main)\n"
        << "  <file.jar>     Executable JAR (incl. Spring Boot fat JAR)\n"
        << "  <file.class>   Compiled class file\n";
}

int parse_args(int argc, char* argv[], LaunchConfig& config) {
    const char sep = classpath_separator();
    int i = 1;

    while (i < argc) {
        std::string arg = argv[i];

        if (arg == "-cp" || arg == "-classpath") {
            if (++i >= argc) { std::cerr << "Error: " << arg << " needs a path\n"; return 1; }
            config.classpath = argv[i++];
        } else if (arg == "-lib") {
            if (++i >= argc) { std::cerr << "Error: -lib needs a directory\n"; return 1; }
            config.lib_dirs.emplace_back(argv[i++]);
        } else if (arg == "--java-home") {
            if (++i >= argc) { std::cerr << "Error: --java-home needs a path\n"; return 1; }
            config.java_home = argv[i++];
        } else if (arg.size() > 2 && arg[0] == '-' && arg[1] == 'J') {
            config.jvm_args.push_back(arg.substr(2));
            i++;
        } else if (arg == "-h" || arg == "--help") {
            return -1;
        } else {
            config.main_class = arg;
            i++;
            while (i < argc) config.app_args.emplace_back(argv[i++]);
        }
    }

    if (config.main_class.empty()) {
        std::cerr << "Error: no target specified\n";
        return 1;
    }

    fs::path target(config.main_class);
    std::string ext = target.extension().string();

    if (ext == ".jar") {
        config.is_jar = true;
        config.main_class = to_native_path(fs::absolute(target).string());
        if (!config.classpath.empty()) config.classpath += sep;
        config.classpath += config.main_class;
    } else if (ext == ".class") {
        std::string abs = to_native_path(fs::absolute(target).string());
        std::string internal = read_class_name(abs);
        if (internal.empty()) {
            std::cerr << "Error: cannot parse class file: " << target << "\n";
            return 1;
        }
        if (config.classpath.empty()) {
            std::string norm = to_forward_slash(abs);
            std::string suffix = internal + ".class";
            if (ends_with_ci(norm, suffix)) {
                config.classpath = norm.substr(0, norm.size() - suffix.size());
            } else {
                config.classpath = to_native_path(fs::absolute(target.parent_path()).string());
            }
        }
        config.main_class = internal;
    } else {
        if (config.classpath.empty()) {
            std::cerr << "Error: -cp is required when specifying a class name\n";
            return 1;
        }
    }

    // scan -lib directories
    for (auto& dir : config.lib_dirs) {
        fs::path lib_dir(dir);
        if (!fs::is_directory(lib_dir)) {
            std::cerr << "Warning: -lib directory not found: " << dir << "\n";
            continue;
        }
        for (auto& entry : fs::directory_iterator(lib_dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".jar") {
                if (!config.classpath.empty()) config.classpath += sep;
                config.classpath += to_native_path(fs::absolute(entry.path()).string());
            }
        }
    }

    return 0;
}

// ============================================================
// JNI helpers
// ============================================================

static std::string jni_to_string(JNIEnv* env, jstring s) {
    if (!s) return {};
    const char* cs = env->GetStringUTFChars(s, nullptr);
    std::string r(cs);
    env->ReleaseStringUTFChars(s, cs);
    return r;
}

static std::string read_jar_main_class(JNIEnv* env, const std::string& jar_path) {
    auto jarFileCls = env->FindClass("java/util/jar/JarFile");
    if (!jarFileCls) return {};

    jstring jpath = env->NewStringUTF(jar_path.c_str());
    auto init = env->GetMethodID(jarFileCls, "<init>", "(Ljava/lang/String;)V");
    jobject jar = env->NewObject(jarFileCls, init, jpath);
    env->DeleteLocalRef(jpath);
    if (!jar) return {};

    auto getMF = env->GetMethodID(jarFileCls, "getManifest", "()Ljava/util/jar/Manifest;");
    jobject mf = env->CallObjectMethod(jar, getMF);
    env->DeleteLocalRef(jar);
    if (!mf) { env->ExceptionClear(); return {}; }

    auto mfCls = env->FindClass("java/util/jar/Manifest");
    auto getAttrs = env->GetMethodID(mfCls, "getMainAttributes", "()Ljava/util/jar/Attributes;");
    jobject attrs = env->CallObjectMethod(mf, getAttrs);
    env->DeleteLocalRef(mf);
    if (!attrs) { env->ExceptionClear(); return {}; }

    auto attrCls = env->FindClass("java/util/jar/Attributes");
    auto getValue = env->GetMethodID(attrCls, "getValue", "(Ljava/lang/String;)Ljava/lang/String;");
    jstring key = env->NewStringUTF("Main-Class");
    jstring val = (jstring)env->CallObjectMethod(attrs, getValue, key);
    env->DeleteLocalRef(key);
    env->DeleteLocalRef(attrs);

    return jni_to_string(env, val);
}

static int run_main(JNIEnv* env, const std::string& class_name, const std::vector<std::string>& app_args) {
    std::string internal = class_name;
    std::replace(internal.begin(), internal.end(), '.', '/');

    jclass cls = env->FindClass(internal.c_str());
    if (!cls) {
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        std::cerr << "Error: class not found: " << class_name << "\n";
        return 1;
    }

    auto main = env->GetStaticMethodID(cls, "main", "([Ljava/lang/String;)V");
    if (!main) {
        if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        std::cerr << "Error: main(String[]) not found in " << class_name << "\n";
        return 1;
    }

    jclass strCls = env->FindClass("java/lang/String");
    jobjectArray args = env->NewObjectArray(static_cast<jsize>(app_args.size()), strCls, nullptr);
    for (jsize j = 0; j < (jsize)app_args.size(); j++) {
        jstring s = env->NewStringUTF(app_args[j].c_str());
        env->SetObjectArrayElement(args, j, s);
        env->DeleteLocalRef(s);
    }

    env->CallStaticVoidMethod(cls, main, args);

    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
        return 1;
    }
    return 0;
}

// ============================================================
// Launch
// ============================================================

static const char* jni_err_msg(jint code) {
    switch (code) {
        case JNI_ERR:       return "unknown error";
        case JNI_EDETACHED: return "thread detached";
        case JNI_EVERSION:  return "JNI version not supported";
        case JNI_ENOMEM:    return "out of memory";
        case JNI_EEXIST:    return "JVM already created";
        case JNI_EINVAL:    return "invalid arguments";
        default:            return "unknown";
    }
}

int launch_jvm(const LaunchConfig& config) {
    std::string home = get_java_home(config.java_home);
    std::string jvm_path = find_jvm_library(home);
    if (jvm_path.empty()) {
        std::cerr << "Error: JVM library not found. Set JAVA_HOME or use --java-home.\n";
        return 1;
    }

    add_library_search_dir(to_native_path((fs::path(home) / "bin").string()));

    void* lib = load_library(jvm_path.c_str());
    if (!lib) {
        std::cerr << "Error: cannot load " << jvm_path << "\n";
        std::string err = load_library_error();
        if (!err.empty()) std::cerr << "  " << err << "\n";
        return 1;
    }

    using CreateVM = jint(*)(JavaVM**, void**, void*);
    auto create_vm = reinterpret_cast<CreateVM>(get_symbol(lib, "JNI_CreateJavaVM"));
    if (!create_vm) {
        std::cerr << "Error: JNI_CreateJavaVM not found in " << jvm_path << "\n";
        return 1;
    }

    std::string cp_opt = "-Djava.class.path=" + to_native_path(config.classpath);

    std::vector<JavaVMOption> opts;
    opts.push_back({const_cast<char*>(cp_opt.c_str()), nullptr});
    for (auto& a : config.jvm_args)
        opts.push_back({const_cast<char*>(a.c_str()), nullptr});

    JavaVMInitArgs vm_args;
    vm_args.version = JNI_VERSION_1_8;
    vm_args.nOptions = static_cast<jint>(opts.size());
    vm_args.options = opts.data();
    vm_args.ignoreUnrecognized = JNI_FALSE;

    JavaVM* jvm = nullptr;
    JNIEnv* env = nullptr;
    jint rc = create_vm(&jvm, reinterpret_cast<void**>(&env), &vm_args);
    if (rc != JNI_OK) {
        std::cerr << "Error: JNI_CreateJavaVM failed (" << jni_err_msg(rc) << ")\n";
        return 1;
    }

    std::string main_class = config.main_class;
    if (config.is_jar) {
        std::string mc = read_jar_main_class(env, config.main_class);
        if (mc.empty()) {
            std::cerr << "Error: Main-Class not found in JAR manifest\n";
            jvm->DestroyJavaVM();
            return 1;
        }
        main_class = mc;
    }

    int result = run_main(env, main_class, config.app_args);
    jvm->DestroyJavaVM();
    return result;
}
