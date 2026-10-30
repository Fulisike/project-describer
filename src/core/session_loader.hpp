#pragma once

#include "core/scanner.hpp"

namespace project_describer {

// Reads all local transcripts, grouped by their recorded project, without opening credentials.
void AppendCodexSessions(ScanResult& result, const std::filesystem::path& codex_home);
void AppendClaudeSessions(ScanResult& result, const std::filesystem::path& claude_home);

}  // namespace project_describer
