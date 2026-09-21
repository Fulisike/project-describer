#pragma once

#include <filesystem>

#include "core/scanner.hpp"

namespace project_describer {

// Discovers the text files that Codex and Claude Code use as durable context.
// Authentication data, conversation transcripts, logs, and caches are
// intentionally outside this boundary.
class ContextLoader {
public:
    static ScanResult LoadCodex(const std::filesystem::path& project_root);
    static ScanResult LoadClaudeCode(const std::filesystem::path& project_root);

    // Explicit roots keep discovery deterministic and make the behavior easy
    // to test. codex_home is normally ~/.codex and claude_home ~/.claude.
    static ScanResult LoadCodex(const std::filesystem::path& project_root,
                                const std::filesystem::path& codex_home);
    static ScanResult LoadClaudeCode(const std::filesystem::path& project_root,
                                     const std::filesystem::path& claude_home,
                                     const std::filesystem::path& managed_root = {});
};

}  // namespace project_describer
