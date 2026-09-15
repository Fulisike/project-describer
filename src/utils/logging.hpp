#pragma once

#include <memory>
#include <string>

#include <spdlog/spdlog.h>

namespace project_describer::log {

enum class Mode { kCli, kGui };

/// Call once at startup, before any other logging operation.
void Initialize(Mode mode, const std::string& log_dir);
void Shutdown();

/// The project-wide spdlog logger instance.
std::shared_ptr<spdlog::logger> Logger();

/// Which frontend mode we are running under.
Mode GetMode();

/// Format a brief location string (caller is responsible for __FILE__ / __LINE__).
constexpr std::string_view ShortFile(std::string_view path) noexcept;

// ---------------------------------------------------------------------------
// Convenience macros that capture source location automatically.
// ---------------------------------------------------------------------------
#define LOG_TRACE(...)    SPDLOG_LOGGER_CALL(::project_describer::log::Logger().get(), spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...)    SPDLOG_LOGGER_CALL(::project_describer::log::Logger().get(), spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...)     SPDLOG_LOGGER_CALL(::project_describer::log::Logger().get(), spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...)     SPDLOG_LOGGER_CALL(::project_describer::log::Logger().get(), spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...)    SPDLOG_LOGGER_CALL(::project_describer::log::Logger().get(), spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_LOGGER_CALL(::project_describer::log::Logger().get(), spdlog::level::critical, __VA_ARGS__)

}  // namespace project_describer::log
