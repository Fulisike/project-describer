#include "utils/logging.hpp"

#include <filesystem>
#include <system_error>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace project_describer::log {
namespace {

Mode g_mode = Mode::kCli;
std::shared_ptr<spdlog::logger> g_logger;

}  // namespace

void Initialize(const Mode mode, const std::string& log_dir) {
    g_mode = mode;

    // File sink: {log_dir}/describer.log, rotate-free for simplicity on v1
    std::string log_path = (std::filesystem::path(log_dir) / "describer.log").string();
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path, true);

    // Console / IDE sink depends on mode
    std::shared_ptr<spdlog::sinks::sink> console_sink;
    if (mode == Mode::kCli) {
        console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    } else {
        // GUI mode: no console window, but we still send to VS Output window
        console_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    }

    // Combine sinks: console shows info+, file gets everything
    spdlog::sinks_init_list sink_list = {console_sink, file_sink};
    g_logger = std::make_shared<spdlog::logger>("project-describer", sink_list);

    // Set per-sink levels
    console_sink->set_level(spdlog::level::info);
    file_sink->set_level(spdlog::level::trace);  // file captures everything

    // Pattern: [timestamp] [level] [file:line] message
    constexpr std::string_view pattern = "%Y-%m-%d %H:%M:%S.%e";
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%s:%#] %v");

    g_logger->set_level(spdlog::level::trace);
    spdlog::register_logger(g_logger);

    LOG_INFO("Log initialized (mode={}, path={})",
             mode == Mode::kCli ? "cli" : "gui",
             log_path);
}

void Shutdown() {
    if (g_logger) {
        g_logger->flush();
        spdlog::drop(g_logger->name());
        g_logger.reset();
    }
}

std::shared_ptr<spdlog::logger> Logger() {
    return g_logger;
}

Mode GetMode() {
    return g_mode;
}

}  // namespace project_describer::log
