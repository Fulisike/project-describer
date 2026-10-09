#include "core/context_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "utils/file_reader.hpp"
#include "utils/logging.hpp"
#include "core/session_loader.hpp"

namespace project_describer {
namespace {

constexpr std::uintmax_t kMaxContextFileSize = 4 * 1024 * 1024;

void GroupSharedContext(ScanResult& result) {
    auto group = [](const std::filesystem::path& path) {
        auto component = path.begin();
        std::filesystem::path grouped = *component++;
        grouped /= "Shared context";
        for (; component != path.end(); ++component) {
            grouped /= *component;
        }
        return grouped;
    };
    for (auto& file : result.text_files) {
        file.relative_path = group(file.relative_path);
    }
    for (auto& entry : result.entries) {
        entry.relative_path = group(entry.relative_path);
    }
    if (!result.entries.empty()) {
        result.entries.push_back({*result.entries.front().relative_path.begin(), true});
    }
}

std::filesystem::path HomeDirectory() {
#ifdef _WIN32
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, L"USERPROFILE") == 0 && value != nullptr) {
        const std::filesystem::path path(value);
        std::free(value);
        return path;
    }
#else
    if (const char* value = std::getenv("HOME")) {
        return std::filesystem::path(value);
    }
#endif
    return {};
}

#ifdef _WIN32
std::filesystem::path EnvironmentPath(const wchar_t* name,
                                      const std::filesystem::path& fallback) {
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, name) == 0 && value != nullptr) {
        const std::filesystem::path path(value);
        std::free(value);
        return path;
    }
    return fallback;
}
#else
std::filesystem::path EnvironmentPath(const char* name,
                                      const std::filesystem::path& fallback) {
    if (const char* value = std::getenv(name); value != nullptr && *value != '\0') {
        return std::filesystem::path(value);
    }
    return fallback;
}
#endif

std::filesystem::path Utf8Path(std::string_view value) {
    const auto* first = reinterpret_cast<const char8_t*>(value.data());
    return std::filesystem::path(std::u8string(first, first + value.size()));
}

std::string PathKey(const std::filesystem::path& path) {
    std::error_code error;
    auto normalized = std::filesystem::weakly_canonical(path, error);
    if (error) {
        normalized = path.lexically_normal();
    }
    auto value = normalized.generic_u8string();
    std::string key(value.begin(), value.end());
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
#endif
    return key;
}

void AddEntryPath(ScanResult& result, const std::filesystem::path& relative_path) {
    std::filesystem::path current;
    for (auto it = relative_path.begin(); it != relative_path.end(); ++it) {
        current /= *it;
        const bool is_directory = std::next(it) != relative_path.end();
        const auto duplicate = std::find_if(
            result.entries.begin(), result.entries.end(), [&](const ProjectEntry& entry) {
                return entry.relative_path == current;
            });
        if (duplicate == result.entries.end()) {
            result.entries.push_back({current, is_directory});
        }
    }
}

bool AddFile(ScanResult& result, const std::filesystem::path& source,
             const std::filesystem::path& display_path) {
    const auto duplicate = std::find_if(
        result.text_files.begin(), result.text_files.end(), [&](const FileItem& file) {
            return file.relative_path == display_path;
        });
    if (duplicate != result.text_files.end()) {
        return true;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(source, error) || error) {
        return false;
    }
    const auto size = std::filesystem::file_size(source, error);
    if (error || size > kMaxContextFileSize) {
        result.warnings.push_back("Skipped context file (too large or unreadable): " +
                                  source.string());
        return false;
    }

    const auto read = FileReader::ReadTextFile(source);
    ++result.scanned_files;
    if (!read.success) {
        result.warnings.push_back("Skipped context file: " + source.string() +
                                  " (" + read.error_message + ")");
        return false;
    }

    result.text_files.emplace_back(display_path, read.text.source_encoding,
                                   read.text.utf8);
    AddEntryPath(result, display_path);
    return true;
}

template <typename Predicate>
void AddDirectory(ScanResult& result, const std::filesystem::path& source_root,
                  const std::filesystem::path& display_root, Predicate include) {
    std::error_code error;
    if (!std::filesystem::is_directory(source_root, error) || error) {
        return;
    }

    std::filesystem::recursive_directory_iterator it(
        source_root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error) || error || !include(it->path())) {
            error.clear();
            continue;
        }
        AddFile(result, it->path(), display_root / it->path().lexically_relative(source_root));
    }
    if (error) {
        result.warnings.push_back("Could not completely scan context directory: " +
                                  source_root.string() + " (" + error.message() + ")");
    }
}

bool IsContextText(const std::filesystem::path& path) {
    static const std::set<std::string> kBinaryExtensions = {
        ".db", ".db-shm", ".db-wal", ".sqlite", ".sqlite3", ".lock", ".log",
        ".png", ".jpg", ".jpeg", ".gif", ".ico", ".zip", ".gz", ".pdf"};
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return kBinaryExtensions.count(extension) == 0;
}

std::set<std::string> CodexFallbackNames(const std::filesystem::path& config_path) {
    std::set<std::string> names;
    const auto read = FileReader::ReadTextFile(config_path);
    if (!read.success) {
        return names;
    }
    const std::regex assignment(
        R"(project_doc_fallback_filenames\s*=\s*\[([^\]]*)\])",
        std::regex::icase);
    std::smatch match;
    if (!std::regex_search(read.text.utf8, match, assignment)) {
        return names;
    }
    const std::regex quoted(R"xx(["']([^"']+)["'])xx");
    const std::string values = match[1].str();
    for (std::sregex_iterator it(values.begin(), values.end(), quoted), end;
         it != end; ++it) {
        names.insert((*it)[1].str());
    }
    return names;
}

std::optional<std::filesystem::path> FirstCodexInstruction(
    const std::filesystem::path& directory, const std::set<std::string>& fallback_names) {
    const std::vector<std::string> primary = {"AGENTS.override.md", "AGENTS.md"};
    for (const auto& name : primary) {
        const auto candidate = directory / name;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
    for (const auto& name : fallback_names) {
        const auto candidate = directory / name;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
    return std::nullopt;
}

std::filesystem::path FindRepositoryRoot(const std::filesystem::path& start) {
    auto current = std::filesystem::absolute(start).lexically_normal();
    for (;;) {
        std::error_code error;
        if (std::filesystem::exists(current / ".git", error) && !error) {
            return current;
        }
        const auto parent = current.parent_path();
        if (parent == current || parent.empty()) {
            return std::filesystem::absolute(start).lexically_normal();
        }
        current = parent;
    }
}

std::filesystem::path DisplayAbsolutePath(const std::filesystem::path& prefix,
                                          const std::filesystem::path& absolute_path) {
    std::filesystem::path result = prefix;
    if (!absolute_path.root_name().empty()) {
        std::string drive = absolute_path.root_name().string();
        drive.erase(std::remove(drive.begin(), drive.end(), ':'), drive.end());
        if (!drive.empty()) {
            result /= drive;
        }
    }
    for (const auto& component : absolute_path.relative_path()) {
        result /= component;
    }
    return result;
}

std::vector<std::filesystem::path> ClaudeImports(const std::string& content,
                                                  const std::filesystem::path& source,
                                                  const std::filesystem::path& home) {
    std::vector<std::filesystem::path> paths;
    const std::regex import_pattern(R"((?:^|[\s(])@([^\s`<>]+))",
                                    std::regex::ECMAScript);
    std::istringstream lines(content);
    std::string line;
    bool in_fenced_code = false;
    while (std::getline(lines, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos &&
            (line.compare(first, 3, "```") == 0 || line.compare(first, 3, "~~~") == 0)) {
            in_fenced_code = !in_fenced_code;
            continue;
        }
        if (in_fenced_code) {
            continue;
        }

        bool in_inline_code = false;
        for (char& ch : line) {
            if (ch == '`') {
                in_inline_code = !in_inline_code;
                ch = ' ';
            } else if (in_inline_code) {
                ch = ' ';
            }
        }

        for (std::sregex_iterator it(line.begin(), line.end(), import_pattern), end;
             it != end; ++it) {
            std::string value = (*it)[1].str();
            while (!value.empty() &&
                   (value.back() == ',' || value.back() == '.' || value.back() == ')' ||
                    value.back() == ']' || value.back() == ';' || value.back() == ':')) {
                value.pop_back();
            }
            if (value.empty() || value.find('@') != std::string::npos) {
                continue;
            }
            std::filesystem::path path;
            if (value == "~" || value.rfind("~/", 0) == 0 ||
                value.rfind("~\\", 0) == 0) {
                path = home / Utf8Path(value.substr(value == "~" ? 1 : 2));
            } else {
                path = Utf8Path(value);
                if (path.is_relative()) {
                    path = source.parent_path() / path;
                }
            }
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error) && !error) {
                paths.push_back(path.lexically_normal());
            }
        }
    }
    return paths;
}

void AddClaudeFileAndImports(ScanResult& result, const std::filesystem::path& source,
                             const std::filesystem::path& display_path,
                             const std::filesystem::path& home, int depth,
                             std::set<std::string>& visited) {
    const auto key = PathKey(source);
    if (!visited.insert(key).second) {
        return;
    }
    const auto read = FileReader::ReadTextFile(source);
    if (!read.success) {
        AddFile(result, source, display_path);
        return;
    }
    if (!AddFile(result, source, display_path) || depth >= 4) {
        return;
    }
    for (const auto& imported : ClaudeImports(read.text.utf8, source, home)) {
        AddClaudeFileAndImports(
            result, imported,
            DisplayAbsolutePath(std::filesystem::path("Claude Code") / "Imports", imported),
            home, depth + 1, visited);
    }
}

std::string ClaudeProjectKey(const std::filesystem::path& project_root) {
    auto value = std::filesystem::absolute(project_root).lexically_normal().string();
    for (char& ch : value) {
        if (ch == ':' || ch == '/' || ch == '\\') {
            ch = '-';
        }
    }
    return value;
}

}  // namespace

ScanResult ContextLoader::LoadCodex(const std::filesystem::path& project_root) {
    const auto home = HomeDirectory();
#ifdef _WIN32
    return LoadCodex(project_root, EnvironmentPath(L"CODEX_HOME", home / ".codex"));
#else
    return LoadCodex(project_root, EnvironmentPath("CODEX_HOME", home / ".codex"));
#endif
}

ScanResult ContextLoader::LoadCodex(const std::filesystem::path& project_root,
                                    const std::filesystem::path& codex_home) {
    ScanResult result;
    const auto user_home = codex_home.parent_path();
    const auto fallback_names = CodexFallbackNames(codex_home / "config.toml");

    if (const auto global = FirstCodexInstruction(codex_home, {})) {
        AddFile(result, *global,
                std::filesystem::path("Codex") / "Global" / global->filename());
    }
    AddFile(result, codex_home / "config.toml",
            std::filesystem::path("Codex") / "Global" / "config.toml");
    AddFile(result, codex_home / "requirements.toml",
            std::filesystem::path("Codex") / "Global" / "requirements.toml");
    std::error_code profile_error;
    for (std::filesystem::directory_iterator it(codex_home, profile_error), end;
         !profile_error && it != end; it.increment(profile_error)) {
        if (it->is_regular_file(profile_error) && !profile_error &&
            it->path().filename().string().ends_with(".config.toml")) {
            AddFile(result, it->path(),
                    std::filesystem::path("Codex") / "Global" / it->path().filename());
        }
        profile_error.clear();
    }
    AddDirectory(result, codex_home / "skills",
                 std::filesystem::path("Codex") / "Global" / "skills", IsContextText);
    AddDirectory(result, user_home / ".agents" / "skills",
                 std::filesystem::path("Codex") / "User" / ".agents" / "skills",
                 IsContextText);
    AddDirectory(result, codex_home / "memories",
                 std::filesystem::path("Codex") / "Global" / "memories", IsContextText);

    const auto repository_root = FindRepositoryRoot(project_root);
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        repository_root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    std::set<std::string> visited_directories;
    auto add_directory_context = [&](const std::filesystem::path& directory) {
        if (!visited_directories.insert(PathKey(directory)).second) {
            return;
        }
        if (const auto instruction = FirstCodexInstruction(directory, fallback_names)) {
            AddFile(result, *instruction,
                    std::filesystem::path("Codex") / "Project" /
                        instruction->lexically_relative(repository_root));
        }
        AddFile(result, directory / ".codex" / "config.toml",
                std::filesystem::path("Codex") / "Project" /
                    (directory / ".codex" / "config.toml").lexically_relative(repository_root));
        AddDirectory(result, directory / ".agents" / "skills",
                     std::filesystem::path("Codex") / "Project" /
                         (directory / ".agents" / "skills").lexically_relative(repository_root),
                     IsContextText);
    };
    add_directory_context(repository_root);
    for (; !error && it != end; it.increment(error)) {
        if (it->is_directory(error) && !error) {
            const auto name = it->path().filename().string();
            if (name == ".git" || name == "node_modules" || name == "build" ||
                name == "dist" || name == ".venv") {
                it.disable_recursion_pending();
                continue;
            }
            add_directory_context(it->path());
        }
        error.clear();
    }
    if (error) {
        result.warnings.push_back("Could not completely scan Codex project context: " +
                                  error.message());
    }
    GroupSharedContext(result);
    AppendCodexSessions(result, project_root, codex_home);
    LOG_INFO("Loaded {} Codex context files", result.text_files.size());
    return result;
}

ScanResult ContextLoader::LoadClaudeCode(const std::filesystem::path& project_root) {
    const auto home = HomeDirectory();
#ifdef _WIN32
    const auto claude_home = EnvironmentPath(L"CLAUDE_CONFIG_DIR", home / ".claude");
#else
    const auto claude_home = EnvironmentPath("CLAUDE_CONFIG_DIR", home / ".claude");
#endif
#ifdef _WIN32
    const auto managed_root = std::filesystem::path("C:/Program Files/ClaudeCode");
#else
    const auto managed_root = std::filesystem::path("/etc/claude-code");
#endif
    return LoadClaudeCode(project_root, claude_home, managed_root);
}

ScanResult ContextLoader::LoadClaudeCode(const std::filesystem::path& project_root,
                                         const std::filesystem::path& claude_home,
                                         const std::filesystem::path& managed_root) {
    ScanResult result;
    const auto home = HomeDirectory();
    std::set<std::string> imported;

    AddClaudeFileAndImports(result, managed_root / "CLAUDE.md",
                            std::filesystem::path("Claude Code") / "Managed" / "CLAUDE.md",
                            home, 0, imported);
    AddFile(result, managed_root / "managed-settings.json",
            std::filesystem::path("Claude Code") / "Managed" / "managed-settings.json");

    AddClaudeFileAndImports(result, claude_home / "CLAUDE.md",
                            std::filesystem::path("Claude Code") / "Global" / "CLAUDE.md",
                            home, 0, imported);
    AddFile(result, claude_home / "settings.json",
            std::filesystem::path("Claude Code") / "Global" / "settings.json");
    const std::vector<std::string> global_directories = {
        "rules", "skills", "commands", "agents", "output-styles", "agent-memory"};
    for (const auto& name : global_directories) {
        AddDirectory(result, claude_home / name,
                     std::filesystem::path("Claude Code") / "Global" / name,
                     IsContextText);
    }

    const auto memory_root = claude_home / "projects" / ClaudeProjectKey(project_root) / "memory";
    AddDirectory(result, memory_root,
                 std::filesystem::path("Claude Code") / "Auto memory", IsContextText);

    std::error_code error;
    std::filesystem::recursive_directory_iterator it(
        project_root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    auto add_project_file = [&](const std::filesystem::path& path) {
        const auto display = std::filesystem::path("Claude Code") / "Project" /
                             path.lexically_relative(project_root);
        const auto filename = path.filename().string();
        if (filename == "CLAUDE.md" || filename == "CLAUDE.local.md" ||
            filename == "AGENTS.md") {
            AddClaudeFileAndImports(result, path, display, home, 0, imported);
        } else {
            AddFile(result, path, display);
        }
    };
    for (const auto& name : {"CLAUDE.md", "CLAUDE.local.md", ".mcp.json",
                             ".worktreeinclude"}) {
        add_project_file(project_root / name);
    }
    AddClaudeFileAndImports(
        result, project_root / ".claude" / "CLAUDE.md",
        std::filesystem::path("Claude Code") / "Project" / ".claude" / "CLAUDE.md",
        home, 0, imported);
    AddDirectory(result, project_root / ".claude",
                 std::filesystem::path("Claude Code") / "Project" / ".claude",
                 IsContextText);

    for (; !error && it != end; it.increment(error)) {
        const auto path = it->path();
        if (it->is_directory(error) && !error) {
            const auto name = path.filename().string();
            if (name == ".git" || name == "node_modules" || name == "build" ||
                name == "dist" || name == ".venv" || name == ".claude") {
                it.disable_recursion_pending();
            }
            continue;
        }
        error.clear();
        const auto name = path.filename().string();
        if (name == "CLAUDE.md" || name == "CLAUDE.local.md" || name == "AGENTS.md") {
            add_project_file(path);
        }
    }
    if (error) {
        result.warnings.push_back("Could not completely scan Claude project context: " +
                                  error.message());
    }
    GroupSharedContext(result);
    AppendClaudeSessions(result, project_root, claude_home);
    LOG_INFO("Loaded {} Claude Code context files", result.text_files.size());
    return result;
}

}  // namespace project_describer
