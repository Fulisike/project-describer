#include "core/context_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>

#include "utils/file_reader.hpp"
#include "utils/logging.hpp"
#include "utils/path_utils.hpp"
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
        return path.empty() ? fallback : path;
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
    const auto name = PathToUtf8(source.filename());
    if (name == "auth.json" || name == ".credentials.json" || name == "credentials.json") {
        return false;
    }
    const auto duplicate = std::find_if(
        result.text_files.begin(), result.text_files.end(), [&](const FileItem& file) {
            return file.relative_path == display_path;
        });
    if (duplicate != result.text_files.end()) {
        return true;
    }
    std::error_code error;
    if (IsPathLink(source) || IsPathLink(source.parent_path()) ||
        !std::filesystem::is_regular_file(source, error) || error) {
        return false;
    }
    const auto size = std::filesystem::file_size(source, error);
    if (error || size > kMaxContextFileSize) {
        result.warnings.push_back("Skipped context file (too large or unreadable): " +
                                  PathToUtf8(source));
        return false;
    }

    const auto read = FileReader::ReadTextFile(source, kMaxContextFileSize);
    ++result.scanned_files;
    if (!read.success) {
        result.warnings.push_back("Skipped context file: " + PathToUtf8(source) +
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
    if (IsPathLink(source_root) || !std::filesystem::is_directory(source_root, error) || error) {
        return;
    }

    std::filesystem::recursive_directory_iterator it(
        source_root, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && it != end; it.increment(error)) {
        const auto name = PathToUtf8(it->path().filename());
        if (IsPathLink(it->path()) || name == "cache" || name == "caches" ||
            name == ".cache" || name == "logs" || name == "auth.json" ||
            name == ".credentials.json" || name == "credentials.json") {
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(error) || error || !include(it->path())) {
            error.clear();
            continue;
        }
        AddFile(result, it->path(), display_root / it->path().lexically_relative(source_root));
    }
    if (error) {
        result.warnings.push_back("Could not completely scan context directory: " +
                                  PathToUtf8(source_root) + " (" + ErrorToUtf8(error) + ")");
    }
}

bool IsContextText(const std::filesystem::path& path) {
    static const std::set<std::string> kBinaryExtensions = {
        ".db", ".db-shm", ".db-wal", ".sqlite", ".sqlite3", ".lock", ".log",
        ".png", ".jpg", ".jpeg", ".gif", ".ico", ".zip", ".gz", ".pdf"};
    auto extension = PathToUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return kBinaryExtensions.count(extension) == 0;
}

std::vector<std::string> CodexFallbackNames(const std::filesystem::path& config_path) {
    std::vector<std::string> names;
    const auto read = FileReader::ReadTextFile(config_path, kMaxContextFileSize);
    if (!read.success) {
        return names;
    }
    // Remove TOML comments without stripping '#' inside quoted filenames.
    std::string config;
    char quote = 0;
    bool escaped = false;
    bool comment = false;
    for (const char ch : read.text.utf8) {
        if (comment && ch != '\n') continue;
        if (ch == '\n') comment = false;
        if (!quote && ch == '#') { comment = true; continue; }
        config += ch;
        if (escaped) { escaped = false; continue; }
        if (quote == '"' && ch == '\\') { escaped = true; continue; }
        if (quote && ch == quote) quote = 0;
        else if (!quote && (ch == '"' || ch == '\'')) quote = ch;
    }
    constexpr std::string_view key = "project_doc_fallback_filenames";
    std::size_t cursor = 0;
    bool found = false;
    while (cursor < config.size()) {
        const auto end = config.find('\n', cursor);
        const auto start = config.find_first_not_of(" \t\r", cursor);
        if (start != std::string::npos && (end == std::string::npos || start < end)) {
            if (config[start] == '[') break;  // Only the top-level TOML setting applies.
            if (config.compare(start, key.size(), key) == 0) {
                cursor = config.find_first_not_of(" \t", start + key.size());
                if (cursor == std::string::npos || config[cursor] != '=') break;
                cursor = config.find_first_not_of(" \t\r\n", cursor + 1);
                if (cursor == std::string::npos || config[cursor] != '[') break;
                ++cursor;
                found = true;
                break;
            }
        }
        if (end == std::string::npos) break;
        cursor = end + 1;
    }
    if (!found) return names;
    while (cursor < config.size()) {
        cursor = config.find_first_not_of(" \t\r\n,", cursor);
        if (cursor == std::string::npos || config[cursor] == ']') break;
        const auto start = cursor++;
        const char delimiter = config[start];
        if (delimiter != '\'' && delimiter != '"') break;
        while (cursor < config.size() && config[cursor] != delimiter) {
            if (delimiter == '"' && config[cursor] == '\\') ++cursor;
            ++cursor;
        }
        if (cursor >= config.size()) break;
        std::string name;
        if (delimiter == '"') {
            const auto value = nlohmann::json::parse(config.substr(start, cursor - start + 1), nullptr, false);
            if (!value.is_string()) break;
            name = value.get<std::string>();
        } else {
            name = config.substr(start + 1, cursor - start - 1);
        }
        ++cursor;
        const auto path = Utf8Path(name);
        if (!name.empty() && name != "." && name != ".." &&
            name.find_first_of("/\\:\0", 0, 4) == std::string::npos &&
            !path.has_parent_path()) {
            names.push_back(name);
        }
    }
    return names;
}

std::optional<std::filesystem::path> FirstCodexInstruction(
    const std::filesystem::path& directory, const std::vector<std::string>& fallback_names) {
    const std::vector<std::string> primary = {"AGENTS.override.md", "AGENTS.md"};
    for (const auto& name : primary) {
        const auto candidate = directory / name;
        std::error_code error;
        if (!IsPathLink(candidate) && std::filesystem::is_regular_file(candidate, error) && !error) {
            return candidate;
        }
    }
    for (const auto& name : fallback_names) {
        const auto candidate = directory / Utf8Path(name);
        std::error_code error;
        if (!IsPathLink(candidate) && std::filesystem::is_regular_file(candidate, error) && !error) {
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
        std::string drive = PathToUtf8(absolute_path.root_name());
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
    std::istringstream lines(content);
    std::string line;
    char fence = 0;
    std::size_t fence_width = 0;
    while (std::getline(lines, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first != std::string::npos && (line[first] == '`' || line[first] == '~')) {
            const auto end = line.find_first_not_of(line[first], first);
            const auto width = (end == std::string::npos ? line.size() : end) - first;
            if (width >= 3) {
                if (fence == 0) {
                    fence = line[first];
                    fence_width = width;
                } else if (fence == line[first] && width >= fence_width &&
                           (end == std::string::npos || line.find_first_not_of(" \t\r", end) == std::string::npos)) {
                    fence = 0;
                }
                continue;
            }
        }
        if (fence != 0) {
            continue;
        }

        for (std::size_t cursor = 0; cursor < line.size();) {
            const auto start = line.find('`', cursor);
            if (start == std::string::npos) break;
            auto end = line.find_first_not_of('`', start);
            if (end == std::string::npos) break;
            const auto width = end - start;
            cursor = end;
            while ((end = line.find('`', end)) != std::string::npos) {
                const auto next = line.find_first_not_of('`', end);
                const auto stop = next == std::string::npos ? line.size() : next;
                if (stop - end == width) {
                    std::fill(line.begin() + start, line.begin() + stop, ' ');
                    cursor = stop;
                    break;
                }
                end = stop;
            }
        }

        for (std::size_t cursor = 0; cursor < line.size();) {
            const auto at = line.find('@', cursor);
            if (at == std::string::npos) break;
            cursor = at + 1;
            if (at != 0 && line[at - 1] != '(' &&
                !std::isspace(static_cast<unsigned char>(line[at - 1]))) continue;
            const auto end = line.find_first_of(" \t\r\n\v\f`<>", cursor);
            std::string value = line.substr(cursor, end == std::string::npos ? end : end - cursor);
            cursor = end == std::string::npos ? line.size() : end;
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
    if (!AddFile(result, source, display_path) || depth >= 4) {
        return;
    }
    const auto file = std::find_if(result.text_files.begin(), result.text_files.end(),
        [&](const FileItem& item) { return item.relative_path == display_path; });
    const auto imports = ClaudeImports(file->content, source, home);
    for (const auto& imported : imports) {
        AddClaudeFileAndImports(
            result, imported,
            DisplayAbsolutePath(std::filesystem::path("Claude Code") / "Imports", imported),
            home, depth + 1, visited);
    }
}

std::string ClaudeProjectKey(const std::filesystem::path& project_root) {
    auto value = PathToUtf8(std::filesystem::absolute(project_root).lexically_normal());
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
            PathToUtf8(it->path().filename()).ends_with(".config.toml")) {
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
        if (IsPathLink(it->path())) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(error) && !error) {
            const auto name = PathToUtf8(it->path().filename());
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
                                  ErrorToUtf8(error));
    }
    GroupSharedContext(result);
    AppendCodexSessions(result, codex_home);
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

    if (!managed_root.empty()) {
        AddClaudeFileAndImports(result, managed_root / "CLAUDE.md",
                                std::filesystem::path("Claude Code") / "Managed" / "CLAUDE.md",
                                home, 0, imported);
        AddFile(result, managed_root / "managed-settings.json",
                std::filesystem::path("Claude Code") / "Managed" / "managed-settings.json");
    }

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
        const auto filename = PathToUtf8(path.filename());
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
    if (!IsPathLink(project_root / ".claude")) {
        AddClaudeFileAndImports(
            result, project_root / ".claude" / "CLAUDE.md",
            std::filesystem::path("Claude Code") / "Project" / ".claude" / "CLAUDE.md",
            home, 0, imported);
    }
    AddDirectory(result, project_root / ".claude",
                 std::filesystem::path("Claude Code") / "Project" / ".claude",
                 IsContextText);

    for (; !error && it != end; it.increment(error)) {
        const auto path = it->path();
        if (IsPathLink(path)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory(error) && !error) {
            const auto name = PathToUtf8(path.filename());
            if (name == ".git" || name == "node_modules" || name == "build" ||
                name == "dist" || name == ".venv") {
                it.disable_recursion_pending();
            } else if (name == ".claude") {
                AddClaudeFileAndImports(result, path / "CLAUDE.md",
                    std::filesystem::path("Claude Code") / "Project" /
                        (path / "CLAUDE.md").lexically_relative(project_root), home, 0, imported);
                AddDirectory(result, path,
                    std::filesystem::path("Claude Code") / "Project" /
                        path.lexically_relative(project_root), IsContextText);
                it.disable_recursion_pending();
            }
            continue;
        }
        error.clear();
        const auto name = PathToUtf8(path.filename());
        if (name == "CLAUDE.md" || name == "CLAUDE.local.md" || name == "AGENTS.md") {
            add_project_file(path);
        }
    }
    if (error) {
        result.warnings.push_back("Could not completely scan Claude project context: " +
                                  ErrorToUtf8(error));
    }
    GroupSharedContext(result);
    AppendClaudeSessions(result, claude_home);
    LOG_INFO("Loaded {} Claude Code context files", result.text_files.size());
    return result;
}

}  // namespace project_describer
