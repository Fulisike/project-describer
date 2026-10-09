#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "core/scanner.hpp"
#include "utils/logging.hpp"

int main(int argc, char** argv) {
    project_describer::log::Initialize(project_describer::log::Mode::kCli,
                                       std::filesystem::temp_directory_path().string());
    project_describer::log::Logger()->set_level(spdlog::level::off);
    const auto fixture = std::filesystem::temp_directory_path() /
        ("project-describer-benchmark-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto root = argc > 1 ? std::filesystem::path(argv[1]) : fixture;
    if (argc == 1) {
        const std::string content(8192, 'a');
        for (int directory = 0; directory < 128; ++directory) {
            const auto path = root / std::to_string(directory);
            std::filesystem::create_directories(path);
            for (int file = 0; file < 32; ++file) {
                std::ofstream(path / (std::to_string(file) + ".txt"), std::ios::binary)
                    << content;
            }
        }
    }
    std::vector<double> timings;
    std::size_t files = 0;
    project_describer::ScannerConfig config;
    config.worker_count = argc > 2 ? std::stoull(argv[2]) : 0;
    for (int run = 0; run < 6; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = project_describer::Scanner::Scan(root, root, config);
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (run != 0) {
            timings.push_back(milliseconds);
        }
        files = result.text_files.size();
        if (!result.warnings.empty()) {
            std::cerr << "Scan warnings: " << result.warnings.size() << '\n';
        }
    }
    std::sort(timings.begin(), timings.end());
    std::cout << "workers=" << config.worker_count << " files=" << files
              << " median_ms=" << timings[timings.size() / 2]
              << " min_ms=" << timings.front() << " max_ms=" << timings.back() << '\n';
    if (argc == 1) {
        std::filesystem::remove_all(fixture);
    }
    project_describer::log::Shutdown();
}
