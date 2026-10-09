#pragma once

#include "core/scanner.hpp"

namespace project_describer {

// Reads local transcripts for the selected working directory, without opening credentials.
void AppendCodexSessions(ScanResult& result, const std::filesystem::path& project_root,
                         const std::filesystem::path& codex_home);
void AppendClaudeSessions(ScanResult& result, const std::filesystem::path& project_root,
                          const std::filesystem::path& claude_home);

}  // namespace project_describer
