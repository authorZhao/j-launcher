#include "jvm_launcher.h"
#include "platform.h"

#include <iostream>
#include <chrono>
#include <iomanip>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "[j-launcher] executable: " << platform::get_executable_path() << std::endl;

    LaunchConfig config;
    int rc = parse_args(argc, argv, config);
    if (rc == -1) {
        print_usage(argv[0]);
        return 0;
    }
    if (rc != 0) return rc;

    auto t0 = std::chrono::steady_clock::now();
    rc = launch_jvm(config);
    auto t1 = std::chrono::steady_clock::now();

    double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "[j-launcher] finished in " << elapsed << "s (exit code " << rc << ")" << std::endl;

    return rc;
}
