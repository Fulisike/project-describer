#include "utils/logging.hpp"

#include <filesystem>
#include <system_error>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "utils/path_utils.hpp"

namespace project_describer::log {
namespace {

Mode g_mode = Mode::kCli;
std::shared_ptr<spdlog::logger> g_logger;

}  // namespace

void Initialize(const Mode mode, const std::filesystem::path& log_dir) {
    Shutdown();
    g_mode = mode;

    // Console / IDE sink depends on mode
    std::shared_ptr<spdlog::sinks::sink> console_sink;
    if (mode == Mode::kCli) {
        console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    } else {
        // GUI mode: no console window, but we still send to VS Output window
#ifdef _WIN32
        console_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
        console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
#endif
    }

    // Combine sinks: console shows info+, file gets everything
    console_sink->set_level(spdlog::level::info);
    g_logger = std::make_shared<spdlog::logger>("project-describer", console_sink);
    const auto log_path = log_dir / "describer.log";
    try {
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.native(), false);
        file_sink->set_level(spdlog::level::trace);
        g_logger->sinks().push_back(file_sink);
    } catch (const spdlog::spdlog_ex& error) {
        g_logger->warn("File logging unavailable: {}", error.what());
    }
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    g_logger->set_level(spdlog::level::trace);
    spdlog::register_logger(g_logger);

    LOG_INFO("Log initialized (mode={}, path={})",
             mode == Mode::kCli ? "cli" : "gui",
             PathToUtf8(log_path));
}

void Shutdown() {
    if (g_logger) {
        g_logger->flush();
        spdlog::drop(g_logger->name());
        g_logger.reset();
    }
}

std::shared_ptr<spdlog::logger> Logger() {
    return g_logger ? g_logger : spdlog::default_logger();
}

Mode GetMode() {
    return g_mode;
}

}  // namespace project_describer::log
