# j-launcher — 用 C/C++ 内嵌 JVM，把 Java 当脚本运行

## 项目简介

j-launcher 是一个轻量级的 JVM 启动器，通过 JNI（Java Native Interface）在 C++ 进程内动态创建 JVM，直接运行编译好的 `.class` 文件、可执行 `.jar` 包（包括 Spring Boot fat JAR）。整个过程不需要 `java` 命令，程序本身就是 JVM 的宿主。

## 目录结构

```
j-launcher/
├── CMakeLists.txt              # 构建配置
├── include/
│   ├── jvm_launcher.h          # 业务逻辑接口
│   └── platform.h              # 平台抽象接口
├── src/
│   ├── main.cpp                # 程序入口、计时、打印
│   ├── jvm_launcher.cpp        # 核心逻辑：参数解析、JVM 启动、JNI 调用
│   └── platform/
│       ├── win/platform.cpp    # Windows 实现（MSVC / MinGW / Cygwin）
│       ├── linux/platform.cpp  # Linux 实现
│       └── mac/platform.cpp    # macOS 实现
├── java/
│   └── Main.java               # 测试用 Java 源码
├── resources/
│   ├── app.rc                  # Windows 资源文件（图标）
│   ├── java.ico
└── docs/
    └── README.md               # 本文档
```

## 核心原理：C/C++ 内嵌 JVM

### 整体流程

```
┌─────────────────────────────────────────────────────────┐
│                    j-launcher 启动                        │
├─────────────────────────────────────────────────────────┤
│  1. 解析命令行参数（目标文件、classpath、JVM 参数等）       │
│  2. 定位 JVM 共享库（jvm.dll / libjvm.so / libjvm.dylib）│
│  3. 动态加载 JVM 库，获取 JNI_CreateJavaVM 函数指针        │
│  4. 调用 JNI_CreateJavaVM 创建 JVM 实例                   │
│  5. 通过 JNI 调用目标类的 main(String[]) 方法              │
│  6. 销毁 JVM，退出                                        │
└─────────────────────────────────────────────────────────┘
```

### 第一步：定位 JVM 共享库

不链接 JVM，而是运行时动态加载。根据 JAVA_HOME 在候选路径中搜索：

| 平台 | 候选路径（相对于 JAVA_HOME） |
|------|------------------------------|
| Windows | `bin/server/jvm.dll`、`jre/bin/server/jvm.dll`、`bin/client/jvm.dll` |
| Linux | `lib/server/libjvm.so`、`jre/lib/amd64/server/libjvm.so` 等（含 aarch64/arm/riscv64） |
| macOS | `lib/server/libjvm.dylib`、`Contents/Home/lib/server/libjvm.dylib` 等 |

对应代码在 `src/platform/<os>/platform.cpp` 的 `find_jvm_library()` 中。

### 第二步：动态加载并创建 JVM

```cpp
// 加载 JVM 共享库（Windows 用 LoadLibraryExA，Linux/macOS 用 dlopen）
void* lib = platform::load_library(jvm_path);

// 获取创建函数的指针
using CreateVM = jint(*)(JavaVM**, void**, void*);
auto create_vm = reinterpret_cast<CreateVM>(platform::get_symbol(lib, "JNI_CreateJavaVM"));

// 构建启动参数
std::string cp_opt = "-Djava.class.path=" + classpath;
JavaVMOption opts[] = { {cp_opt.data(), nullptr}, {-Xmx512m, nullptr}, ... };

JavaVMInitArgs vm_args;
vm_args.version = JNI_VERSION_1_8;
vm_args.nOptions = option_count;
vm_args.options = opts;

// 创建 JVM
JavaVM* jvm = nullptr;
JNIEnv* env = nullptr;
create_vm(&jvm, (void**)&env, &vm_args);
```

关键点：
- **classpath 通过 `-Djava.class.path=` 传递**，和命令行 `java -cp` 等价
- **所有 JVM 参数通过 `JavaVMOption` 数组传入**，包括 `-X` 系列和 `-D` 系列属性
- 使用 `JNI_VERSION_1_8` 作为最低版本要求，实际运行的 JVM 版本由 JAVA_HOME 决定

### 第三步：通过 JNI 调用 main 方法

```cpp
// 查找类（使用 / 分隔的内部名称）
jclass cls = env->FindClass("com/example/Main");

// 查找 main 方法
jmethodID main = env->GetStaticMethodID(cls, "main", "([Ljava/lang/String;)V");

// 构造 String[] 参数
jobjectArray args = env->NewObjectArray(argc, stringClass, nullptr);
for (int i = 0; i < argc; i++)
    env->SetObjectArrayElement(args, i, env->NewStringUTF(argv[i]));

// 调用
env->CallStaticVoidMethod(cls, main, args);
```

### 针对 JAR 文件的特殊处理

JAR 文件需要先读取 `META-INF/MANIFEST.MF` 获取 `Main-Class`，再调用。由于解析 ZIP 格式较复杂，直接用 JNI 调用 Java 标准库完成：

```cpp
// new JarFile(jar_path) → getManifest() → getMainAttributes() → getValue("Main-Class")
```

对于 Spring Boot fat JAR，manifest 中的 `Main-Class` 是 `JarLauncher`，它内部会用自定义 ClassLoader 去 `BOOT-INF/classes/` 和 `BOOT-INF/lib/` 加载嵌套 JAR，无需额外处理。

## 运行效果

### 测试用 Java 源码

> 文件：`Main.java`

```java
import java.util.random.RandomGenerator;

public class Main {

    public static void main(String[] args) {
        long start = System.currentTimeMillis();
        var rg = RandomGenerator.of("Xoshiro256PlusPlus");
        var bytes = new byte[1000000000];

        for (int i = 0; i < bytes.length; i++) {
            bytes[i] = (byte) rg.nextInt(200);
        }
        long end = System.currentTimeMillis();
        System.out.printf("Yes!%sms%n", end - start);
    }
}
```

该程序分配 1GB 的 byte 数组并用随机数填充，用来验证 JVM 的正常启动和大内存分配。

### 编译 & 运行

```bash
# 编译 Java
javac Main.java

# 用 j-launcher 运行
j-launcher.exe Main.class
```

### 输出

```
[j-launcher] executable: XXXX\j-launcher.exe
Yes!1851ms
[j-launcher] finished in 1.894s (exit code 0)
```

- 第一行：打印 j-launcher 自身的可执行文件路径
- 第二行：Java 程序的标准输出
- 第三行：打印总耗时（含 JVM 启动 + Java 执行 + JVM 销毁）

## 使用方法

### 基本命令格式

```
j-launcher [options] <target> [app args...]
```

### 运行 class 文件

自动解析 .class 文件提取类名和包结构，推断 classpath 根目录：

```bash
j-launcher build/classes/com/example/Main.class
j-launcher HelloWorld.class
```

### 运行 JAR 包

读取 manifest 中的 Main-Class 并执行，支持 Spring Boot fat JAR：

```bash
j-launcher app.jar
j-launcher springboot-app.jar
```

### 运行指定类名

需要手动指定 classpath：

```bash
# Windows
j-launcher -cp "lib/*" com.example.Main

# Linux/macOS
j-launcher -cp "lib/*" com.example.Main
```

### 添加第三方依赖目录

`-lib` 选项会扫描指定目录下的所有 `.jar` 文件并自动加入 classpath，可多次指定：

```bash
# Windows
j-launcher -lib lib -lib ext app.jar

# Linux/macOS
j-launcher -lib lib -lib ext app.jar
```

### 设置 JVM 启动参数

通过 `-J` 前缀传递参数给 JVM，每个参数一个 `-J`：

```bash
# 堆内存
j-launcher -J-Xmx512m -J-Xms256m app.jar

# 系统属性
j-launcher -J-Dspring.profiles.active=prod -J-Dserver.port=8080 app.jar

# GC 选择
j-launcher -J-XX:+UseG1GC -J-XX:MaxGCPauseMillis=200 app.jar

# 远程调试
j-launcher -J-agentlib:jdwp=transport=dt_socket,server=y,suspend=n,address=5005 app.jar

# 组合使用
j-launcher -J-Xmx1g -J-Dapp.env=prod -lib lib app.jar --server.port=9090
```

`-J` 后面的内容会原样传入 `JavaVMOption`，支持所有合法的 JVM 参数：
- `-X` 系列：`-Xmx`、`-Xms`、`-Xss`、`-Xmn` 等
- `-XX:` 系列：`-XX:+UseG1GC`、`-XX:MetaspaceSize=` 等
- `-D` 系列：系统属性
- `-agentlib:`、`-javaagent:` 等

### 指定 JAVA_HOME

默认读取环境变量 `JAVA_HOME`，可通过参数覆盖：

```bash
j-launcher --java-home /path/to/jdk app.jar
```

### 完整参数列表

```
Options:
  -cp, -classpath <path>   Class search path
  -lib <dir>               Add all jar files in <dir> to classpath (repeatable)
  --java-home <path>       Override JAVA_HOME
  -J<flag>                 Pass flag to JVM (e.g. -J-Xmx512m)
  -h, --help               Show this help

Targets:
  <class-name>   Fully qualified class (e.g. com.example.Main)
  <file.jar>     Executable JAR (incl. Spring Boot fat JAR)
  <file.class>   Compiled class file
```

## 平台抽象层设计

平台相关代码通过 `platform` 命名空间抽象，定义在 `include/platform.h`，各平台独立实现在 `src/platform/<os>/platform.cpp`：

```
platform::get_executable_path()     // 获取当前 exe 路径
platform::to_native_path(path)      // 路径格式转换（Cygwin 需要）
platform::classpath_separator()     // ';' 或 ':'
platform::load_library(path)        // 加载共享库
platform::get_symbol(handle, name)  // 查找符号
platform::load_library_error()      // 获取加载失败的错误信息
platform::add_library_search_dir()  // 添加库搜索路径
platform::find_jvm_library(home)    // 定位 JVM 库
```

这样 `jvm_launcher.cpp` 中完全不需要 `#ifdef`，所有平台差异封装在 platform 层内部。

## 编译指南

### 前置条件

- C++17 编译器（GCC 7+ / Clang 5+ / MSVC 2017+）
- CMake 3.10+
- JDK（设置 `JAVA_HOME` 环境变量）

### Windows

#### MSVC（Visual Studio / CLion）

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

#### MinGW

```bash
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

#### Cygwin

```bash
cmake -B build -G "Unix Makefiles"   # Cygwin 下 CMake 自动识别
cmake --build build
```

### Linux

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

GCC < 9 需要额外链接 `stdc++fs`，CMakeLists.txt 已自动处理。

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 交叉编译

通过 CMake toolchain 文件指定目标平台，`WIN32`/`APPLE`/`UNIX` 变量由 CMake 自动设置，平台源文件会自动选择。

### CLion 配置

CLion 使用 Cygwin/MinGW 工具链时，确认 `Settings → Build → CMake → Environment` 中包含：

```
JAVA_HOME=D:\tool\graalvm-jdk-25.0.3+9.1
```

Reload CMake Project 即可。

## 平台特殊说明

### Windows（MSVC / MinGW / Cygwin）

三种编译环境的差异全部在 `src/platform/win/platform.cpp` 内部处理：

| 特性 | MSVC | MinGW | Cygwin |
|------|------|-------|--------|
| `_WIN32` 宏 | 定义 | 定义 | 可能未定义 |
| `__CYGWIN__` 宏 | 未定义 | 未定义 | 定义 |
| 动态库加载 | `LoadLibraryExA` | `LoadLibraryExA` | `LoadLibraryExA` |
| 路径格式 | Windows 原生 | Windows 原生 | POSIX（需转换） |
| `LOAD_LIBRARY_SEARCH_*` | 直接可用 | 新版可用，旧版 fallback | 需 fallback 定义 |
| `windows.h` | 直接包含 | 直接包含 | 直接包含 |

Cygwin 的路径转换通过 `cygwin_conv_path()` 实现，仅当 `__CYGWIN__` 已定义且 `_WIN32` 未定义时启用。

### Linux

- 使用 `dlopen` / `dlsym` 加载 `libjvm.so`
- 使用 `readlink("/proc/self/exe")` 获取可执行文件路径
- 需要链接 `dl` 库
- JVM 候选路径覆盖 amd64、aarch64、arm、riscv64 架构

### macOS

- 使用 `dlopen` / `dlsym` 加载 `libjvm.dylib`
- 使用 `_NSGetExecutablePath()` 获取可执行文件路径
- JVM 候选路径包含 `Contents/Home/` 子目录（JDK .jdk bundle 结构）

## 设计决策：为什么用动态加载而不是直接链接

有两种方式在 C/C++ 中使用 JVM：

**直接链接（link-time）**
```bash
# 编译时就把 jvm 库绑进二进制
g++ main.cpp -I$JAVA_HOME/include -L$JAVA_HOME/lib/server -ljvm
```
编译器在链接阶段就把 `JNI_CreateJavaVM` 等符号绑死到 `libjvm.so`，程序启动时 OS 加载器必须立刻找到这个库。

**动态加载（runtime）** — 本项目采用的方式
```bash
# 编译时只需要 jni.h 头文件
g++ main.cpp -I$JAVA_HOME/include

# 运行时自己 LoadLibrary/dlopen → GetProcAddress/dlsym
```
编译阶段完全不碰 JVM 库文件，运行时才按需加载。

### 选择动态加载的理由

#### 1. JVM 没有提供标准的链接接口

JDK 不随附导入库。Windows 上没有 `jvm.lib`，Linux 上 `libjvm.so` 藏在 `JAVA_HOME/lib/server/` 这种非标准路径里。要直接链接，需要自己用 `dlltool` 或 `dumpbin` 生成导入库，而且每个 JDK 版本、每个厂商的导出符号可能不同，极其脆弱。

#### 2. 程序的可移植性

直接链接意味着二进制绑死了一个具体的 JVM 路径。用户换了 JDK 版本、换了安装目录、换了 JVM 厂商（Oracle / OpenJDK / GraalVM / Corretto / Zulu），程序就启动不了。

动态加载只依赖 `JAVA_HOME` 环境变量，运行时自动搜索多个候选路径，同一份二进制兼容任意 JDK。

#### 3. 启动失败时能给出有意义的错误信息

直接链接时，OS 加载器找不到 `jvm.dll` / `libjvm.so`，用户看到的是：

```
# Windows 弹窗
"The code execution cannot proceed because jvm.dll was not found."

# Linux
./j-launcher: error while loading shared libraries: libjvm.so: cannot open shared object file
```

用户完全不知道该怎么办。

动态加载时，程序正常启动，错误信息完全可控：

```
Error: JVM library not found. Set JAVA_HOME or use --java-home.
```

#### 4. 加载前可以设置环境

JVM 的加载有前置条件 — Windows 上需要先 `SetDllDirectory` 让 `jvm.dll` 找到它的依赖（`java.dll`、`jimage.dll` 等），Linux 上可能需要调整 `LD_LIBRARY_PATH`。

直接链接时，OS 加载器在代码执行第一行之前就去加载 JVM 了，根本没有机会做这些准备。动态加载让你先准备环境，再加载。

#### 5. JNI 的接口极其小

JVM 对外暴露的 C 接口就 3 个函数：

```
JNI_CreateJavaVM
JNI_GetCreatedJavaVMs
JNI_GetDefaultJavaVMInitArgs
```

为 3 个函数引入编译期依赖不值得。用 `dlsym` / `GetProcAddress` 取一次函数指针，后续调用和直接链接没有区别。

#### 6. 条件性延迟加载

未来如果要支持"没装 JDK 就降级运行"或"按需启动 JVM"的场景，动态加载天然支持 — 不调 `load_library` 就不会加载 JVM，进程更轻量。直接链接则没有这个选择权。

### 对比总结

| | 直接链接 | 动态加载 |
|---|---|---|
| 编译依赖 | 需要 JVM 库文件路径 | 只需 `jni.h` 头文件 |
| 可移植性 | 绑死具体 JVM 路径 | 通过 JAVA_HOME 适配任意 JDK |
| 错误体验 | OS 级崩溃/弹窗 | 可控的错误信息 |
| 环境准备 | 无法在加载前做准备 | 先设环境再加载 |
| 跨厂商兼容 | 每个厂商版本可能要重新编译 | 同一二进制兼容所有厂商 |
| 性能 | 无差异 | 无差异（函数指针间接调用，开销可忽略） |

本质上，JVM 是一个**运行时可选的运行环境**，不是程序的**编译时依赖**。动态加载正确地反映了这个关系。

## 开发环境建议

新版的 IntelliJ IDEA 已支持直接安装 CLion 插件（`Settings → Plugins → 搜索 "CLion"`）。安装后可以在同一个 IDEA 窗口中同时编辑和运行 Java 代码与 C/C++ 代码，对 JNI 开发非常友好：

- Java 侧编写 JNI 接口和业务逻辑，直接在 IDEA 中编译运行
- C/C++ 侧实现 native 方法和 JVM 嵌入逻辑，通过 CLion 插件的 CMake 集成编译调试
- 两侧代码可以在同一个项目中无缝切换，不需要在多个 IDE 之间反复跳转

对于本项目来说，`java/` 目录下的测试代码可以在 IDEA 中直接编译，然后用 j-launcher 运行，全程一个 IDE 搞定。

# 备注
java.ico 仅仅用于测试，网上随便百度的，如有侵权几十联系本人删除。
