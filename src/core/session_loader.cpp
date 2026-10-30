#include "core/session_loader.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string_view>

#include <nlohmann/json.hpp>

#include "utils/path_utils.hpp"

namespace project_describer {
namespace {

using Json = nlohmann::json;
constexpr std::size_t kMaxRecordBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxSessionBytes = 64 * 1024 * 1024;
constexpr std::size_t kMetadataBytes = 2 * 1024 * 1024;

const Json& Field(const Json& value, const char* key) {
    static const Json empty;
    if (!value.is_object()) {
        return empty;
    }
    const auto found = value.find(key);
    return found == value.end() ? empty : *found;
}

std::string String(const Json& value, const char* key) {
    const auto& field = Field(value, key);
    return field.is_string() ? field.get<std::string>() : std::string{};
}

std::filesystem::path Utf8Path(std::string_view text) {
    const auto* begin = reinterpret_cast<const char8_t*>(text.data());
    return std::filesystem::path(std::u8string(begin, begin + text.size()));
}

std::string PathText(const std::filesystem::path& path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

bool IsLink(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes == INVALID_FILE_ATTRIBUTES ||
           (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
    std::error_code error;
    return std::filesystem::is_symlink(path, error) || error;
#endif
}

// A live transcript can grow while it is read. Read only the captured byte budget,
// and cap individual records so an incomplete or huge line cannot allocate indefinitely.
bool ReadLine(std::istream& input, std::string& line, std::size_t& remaining) {
    line.clear();
    auto* buffer = input.rdbuf();
    while (remaining != 0) {
        const auto ch = buffer->sbumpc();
        if (ch == std::char_traits<char>::eof()) {
            return !line.empty();
        }
        --remaining;
        if (ch == '\n') {
            return true;
        }
        if (line.size() >= kMaxRecordBytes) {
            throw std::runtime_error("Session record exceeds 4 MiB limit");
        }
        line.push_back(static_cast<char>(ch));
    }
    return !line.empty();
}

Json Parse(const std::string& line) {
    // Local transcripts are UTF-8 JSONL; the parser decodes escapes and surrogate pairs.
    const auto offset = line.starts_with("\xEF\xBB\xBF") ? 3 : 0;
    return Json::parse(line.begin() + offset, line.end(),
        [](int depth, Json::parse_event_t, Json&) {
            if (depth > 128) throw std::runtime_error("Session JSON nesting exceeds 128 levels");
            return true;
        }, false);
}

Json ReadMetadata(const std::filesystem::path& path, bool codex) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Cannot open session transcript");
    }
    std::size_t remaining = kMetadataBytes;
    std::string line;
    for (int index = 0; index < 128 && ReadLine(input, line, remaining); ++index) {
        auto record = Parse(line);
        if (codex && String(record, "type") == "session_meta") {
            return Field(record, "payload");
        }
        if (!codex && !String(record, "cwd").empty() &&
            !String(record, "sessionId").empty()) {
            return record;
        }
    }
    return {};
}

std::string Pretty(const Json& value) {
    if (value.is_null()) {
        return {};
    }
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        const auto decoded = Parse(text);
        return decoded.is_object() || decoded.is_array() ? decoded.dump(2) : text;
    }
    return value.dump(2);
}

std::string Content(const Json& value) {
    if (value.is_array()) {
        std::string text;
        for (const auto& block : value) {
            text += Content(block) + "\n";
        }
        return text;
    }
    const auto type = String(value, "type");
    if (type == "text" || type == "input_text" || type == "output_text" ||
        type == "summary_text") {
        return String(value, "text");
    }
    if (type == "thinking") {
        return "Thinking:\n" + String(value, "thinking");
    }
    if (type == "image" || type == "input_image") {
        return "[Image attachment; binary image data is not rendered as text]";
    }
    if (type == "tool_use") {
        return "Tool call: " + String(value, "name") + " (" + String(value, "id") +
               ")\n" + Pretty(Field(value, "input"));
    }
    if (type == "tool_result") {
        return "Tool result: " + String(value, "tool_use_id") + "\n" +
               Content(Field(value, "content"));
    }
    return Pretty(value);
}

std::string MessageKey(std::string text) {
    const auto end = text.find_last_not_of("\r\n");
    text.resize(end == std::string::npos ? 0 : end + 1);
    return text;
}

std::string RenderCodex(const Json& record) {
    const auto type = String(record, "type");
    const auto& payload = Field(record, "payload");
    if (type == "event_msg" || type == "token_usage_record") {
        return {};  // UI events duplicate response_items and are not model messages.
    }
    std::string heading = type;
    std::string body;
    if (type == "response_item") {
        const auto item_type = String(payload, "type");
        heading = item_type;
        if (item_type == "message") {
            heading = String(payload, "role");
            const auto phase = String(payload, "phase");
            if (!phase.empty()) {
                heading += " / " + phase;
            }
            body = Content(Field(payload, "content"));
        } else if (item_type == "function_call" || item_type == "custom_tool_call") {
            heading = "Tool call: " + String(payload, "name");
            body = "Call ID: " + String(payload, "call_id") + "\n" +
                Pretty(Field(payload, item_type == "function_call" ? "arguments" : "input"));
        } else if (item_type == "function_call_output" || item_type == "custom_tool_call_output") {
            heading = "Tool result: " + String(payload, "call_id");
            body = Content(Field(payload, "output"));
        } else if (item_type == "reasoning") {
            body = Content(Field(payload, "summary")) + Content(Field(payload, "content"));
            if (!Field(payload, "encrypted_content").is_null()) {
                body += "\n[Encrypted reasoning is unavailable as readable text]\n";
            }
        } else {
            body = Pretty(payload);
        }
    } else if (type == "session_meta") {
        auto details = payload;
        const auto& instructions = Field(payload, "base_instructions");
        body = instructions.is_string() ? instructions.get<std::string>() : String(instructions, "text");
        if (details.is_object()) {
            details.erase("base_instructions");
        }
        body += "\n\n" + Pretty(details);
    } else if (type == "compacted") {
        body = "Compaction summary:\n" + String(payload, "message") + "\n" +
               Pretty(Field(payload, "replacement_history"));
    } else {
        body = Pretty(payload.is_null() ? record : payload);
    }
    return "\n## " + heading + " · " + String(record, "timestamp") + "\n\n" + body + "\n";
}

std::string RenderClaude(const Json& record) {
    const auto type = String(record, "type");
    if (type == "progress" || type == "file-history-snapshot" || type == "queue-operation") {
        return {};
    }
    const auto& message = Field(record, "message");
    const auto role = String(message, "role");
    const auto body = role.empty() ? Pretty(record) : Content(Field(message, "content"));
    return "\n## " + (role.empty() ? type : role) + " · " + String(record, "timestamp") +
           "\n\n" + body + "\n";
}

std::string SafeName(std::string text) {
    for (char& ch : text) {
        if (static_cast<unsigned char>(ch) < 32 || std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) {
            ch = '_';
        }
    }
    if (text.empty() || text == "." || text == "..") {
        return "unknown";
    }
    return text;
}

void AppendSession(ScanResult& result, const std::filesystem::path& source,
                    const std::filesystem::path& home, const std::filesystem::path& project,
                    const Json& metadata, bool codex, std::set<std::string>& seen) {
    auto id = String(metadata, codex ? "id" : "sessionId");
    if (id.empty()) {
        id = String(metadata, "session_id");
    }
    const bool subagent = !codex && source.parent_path().filename() == "subagents";
    const auto agent_id = subagent ? PathText(source.stem()) : std::string{};
    const auto key = subagent ? id + "/subagents/" + agent_id : id;
    if (id.empty() || seen.count(key) != 0) {
        return;
    }
    std::ifstream input(source, std::ios::binary | std::ios::ate);
    const auto size = input ? input.tellg() : std::streampos(-1);
    if (size < 0 || static_cast<std::uintmax_t>(size) > kMaxSessionBytes) {
        result.warnings.push_back("Skipped session (unreadable or exceeds 64 MiB): " + PathText(source));
        return;
    }
    input.seekg(0);
    std::size_t remaining = static_cast<std::size_t>(size);
    std::string text = "# " + std::string(codex ? "Codex" : "Claude Code") + " session " + id +
        "\n\nProject: " + PathText(project) + "\nLocal profile: " + PathText(home) +
        "\nSource: " + PathText(source) +
        "\nAccount / authentication: unknown (not recorded reliably in this transcript).\n"
        "\nThis is a readable local transcript, not an exact reconstruction of the current model context.\n";
    if (subagent) {
        text += "\nSubagent: " + agent_id + "\n";
    }
    std::string line;
    std::size_t malformed = 0;
    std::multiset<std::pair<std::string, std::string>> response_messages;
    struct EventMessage {
        std::size_t offset;
        std::pair<std::string, std::string> key;
        std::string rendered;
    };
    std::vector<EventMessage> fallback_messages;
    std::size_t fallback_bytes = 0;
    while (ReadLine(input, line, remaining)) {
        if (line.empty() || line == "\r") {
            continue;
        }
        const auto record = Parse(line);
        if (record.is_discarded() || !record.is_object()) {
            ++malformed;
            continue;
        }
        if (codex && String(record, "type") == "response_item" &&
            String(Field(record, "payload"), "type") == "message") {
            const auto& payload = Field(record, "payload");
            response_messages.emplace(String(payload, "role"), MessageKey(Content(Field(payload, "content"))));
        }
        if (codex && String(record, "type") == "event_msg") {
            const auto& payload = Field(record, "payload");
            const auto type = String(payload, "type");
            if (type == "user_message" || type == "agent_message") {
                auto message = "\n## " + type + "\n\n" + String(payload, "message") + "\n";
                fallback_bytes += message.size();
                fallback_messages.push_back({text.size(),
                    {type == "user_message" ? "user" : "assistant", MessageKey(String(payload, "message"))},
                    std::move(message)});
            }
        }
        text += codex ? RenderCodex(record) : RenderClaude(record);
        if (text.size() + fallback_bytes > kMaxSessionBytes) {
            throw std::runtime_error("Rendered session exceeds 64 MiB limit");
        }
    }
    if (!fallback_messages.empty()) {
        std::string combined;
        combined.reserve(text.size() + fallback_bytes);
        std::size_t previous = 0;
        for (const auto& event : fallback_messages) {
            const auto duplicate = response_messages.find(event.key);
            if (duplicate != response_messages.end()) {
                response_messages.erase(duplicate);
                continue;
            }
            combined.append(text, previous, event.offset - previous);
            combined += event.rendered;
            previous = event.offset;
        }
        combined.append(text, previous);
        text = std::move(combined);
    }
    if (remaining != 0) {
        const auto warning = "Session changed or could not be read completely: " + PathText(source);
        result.warnings.push_back(warning);
        text += "\n[" + warning + "]\n";
    }
    if (malformed != 0) {
        const auto warning = "Skipped " + std::to_string(malformed) +
                             " malformed/incomplete JSONL records: " + PathText(source);
        result.warnings.push_back(warning);
        text += "\n[" + warning + "]\n";
    }
    auto display = std::filesystem::path(codex ? "Codex" : "Claude Code") /
                   "Sessions" / "Local profile (auth unknown)";
    if (codex) {
        display /= Utf8Path("Provider - " + SafeName(String(metadata, "model_provider")));
    }
    for (const auto& component : project.lexically_normal()) {
        const auto name = PathText(component);
        if (name != "/" && name != "\\") {
            display /= Utf8Path(SafeName(name));
        }
    }
    if (subagent) {
        display /= Utf8Path(SafeName(id));
        display /= "Subagents";
    }
    const auto document_id = subagent ? agent_id : id;
    display /= Utf8Path(SafeName(document_id) + ".md");
    const auto directory = display.parent_path();
    for (std::size_t suffix = 1; std::any_of(result.text_files.begin(), result.text_files.end(),
             [&](const FileItem& file) { return file.relative_path == display; }); ++suffix) {
        display = directory / Utf8Path(SafeName(document_id) + "-" + std::to_string(suffix) + ".md");
    }
    result.text_files.emplace_back(display, TextEncoding::kUtf8, std::move(text));
    seen.insert(key);
    std::filesystem::path current;
    for (auto part = display.begin(); part != display.end(); ++part) {
        current /= *part;
        if (std::none_of(result.entries.begin(), result.entries.end(), [&](const ProjectEntry& entry) {
                return entry.relative_path == current;
            })) {
            result.entries.push_back({current, std::next(part) != display.end()});
        }
    }
    ++result.scanned_files;
}

void AppendSessions(ScanResult& result, const std::filesystem::path& home, bool codex) {
    std::set<std::string> seen;
    for (const auto* directory : codex ? std::vector<const char*>{"sessions", "archived_sessions"}
                                      : std::vector<const char*>{"projects"}) {
        const auto root = home / directory;
        std::error_code error;
        if (IsLink(root) || !std::filesystem::is_directory(root, error)) {
            continue;
        }
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, error), end;
        std::vector<std::filesystem::path> candidates;
        for (; !error && it != end; it.increment(error)) {
            const auto path = it->path();
            if (IsLink(path)) {
                it.disable_recursion_pending();
                continue;
            }
            if (it->is_directory(error)) {
                if (!codex && (it.depth() >= 3 ||
                    (it.depth() == 2 && path.filename() != "subagents") ||
                    (it.depth() == 1 && !std::filesystem::is_directory(path / "subagents", error)))) {
                    it.disable_recursion_pending();
                }
                error.clear();
            } else if (!error && path.extension() == ".jsonl" && it->is_regular_file(error)) {
                candidates.push_back(path);
            }
        }
        if (error) {
            result.warnings.push_back("Incomplete session discovery: " + PathText(root));
        }
        std::sort(candidates.begin(), candidates.end());
        for (const auto& source : candidates) {
            try {
                const auto metadata = ReadMetadata(source, codex);
                const auto cwd = String(metadata, "cwd");
                if (cwd.empty()) {
                    result.warnings.push_back("Session has no recognizable project metadata: " + PathText(source));
                }
                if (!cwd.empty()) {
                    AppendSession(result, source, home, Utf8Path(cwd), metadata, codex, seen);
                }
            } catch (const std::exception& error) {
                result.warnings.push_back("Skipped session: " + PathText(source) +
                                          " (" + error.what() + ")");
            }
        }
    }
}

}  // namespace

void AppendCodexSessions(ScanResult& result, const std::filesystem::path& codex_home) {
    AppendSessions(result, codex_home, true);
}

void AppendClaudeSessions(ScanResult& result, const std::filesystem::path& claude_home) {
    AppendSessions(result, claude_home, false);
}

}  // namespace project_describer
