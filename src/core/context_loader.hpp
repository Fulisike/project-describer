#pragma once

#include <filesystem>

#include "core/scanner.hpp"

namespace project_describer {

// Discovers durable instructions and readable local session transcripts.
// Credential files, logs, and caches are outside this boundary.
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
