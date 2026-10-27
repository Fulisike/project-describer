#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/context_loader.hpp"
#include "core/session_loader.hpp"

namespace {

class ScopedContextDirectory {
public:
    ScopedContextDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("project-describer-context-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path_);
    }
    ~ScopedContextDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void WriteContext(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
}

bool HasPathEnding(const project_describer::ScanResult& result,
                   const std::filesystem::path& suffix) {
    for (const auto& file : result.text_files) {
        const auto path = file.relative_path.lexically_normal();
        auto path_it = path.end();
        auto suffix_it = suffix.end();
        bool matches = true;
        while (suffix_it != suffix.begin()) {
            if (path_it == path.begin()) {
                matches = false;
                break;
            }
            --path_it;
            --suffix_it;
            if (*path_it != *suffix_it) {
                matches = false;
                break;
            }
        }
        if (matches) {
            return true;
        }
    }
    return false;
}

bool HasContent(const project_describer::ScanResult& result, const std::string& content) {
    for (const auto& file : result.text_files) {
        if (file.content == content) {
            return true;
        }
    }
    return false;
}

std::string JsonPath(const std::filesystem::path& path) {
    const auto utf8 = path.generic_u8string();
    std::string quoted = "\"";
    for (char ch : std::string(utf8.begin(), utf8.end())) {
        if (ch == '\\' || ch == '"') {
            quoted += '\\';
        }
        quoted += ch;
    }
    return quoted + "\"";
}

std::string CodexMetadata(const std::filesystem::path& project, const std::string& id) {
    return "{\"type\":\"session_meta\",\"payload\":{\"id\":\"" + id +
        "\",\"cwd\":" + JsonPath(project) +
        ",\"model_provider\":\"openai\",\"base_instructions\":{\"text\":\"rules\\nsecond line\"}}}\n";
}

}  // namespace

TEST(LoadsCodexContextUsingOverrideAndConfiguredFallback) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex_home = temp.path() / "codex";
    std::filesystem::create_directories(project / ".git");

    WriteContext(codex_home / "AGENTS.md", "base global");
    WriteContext(codex_home / "AGENTS.override.md", "override global");
    WriteContext(codex_home / "config.toml",
                 "project_doc_fallback_filenames = [\"TEAM_GUIDE.md\"]\n");
    WriteContext(codex_home / "skills" / "demo" / "SKILL.md", "skill body");
    WriteContext(project / "TEAM_GUIDE.md", "project instructions");

    const auto result = project_describer::ContextLoader::LoadCodex(project, codex_home);

    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("Global") /
                                         "AGENTS.override.md"));
    EXPECT_TRUE(!HasPathEnding(result, std::filesystem::path("Global") / "AGENTS.md"));
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("skills") / "demo" /
                                         "SKILL.md"));
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("Project") /
                                         "TEAM_GUIDE.md"));
}

TEST(LoadsClaudeContextAndFollowsImports) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto claude_home = temp.path() / "claude";
    const auto managed = temp.path() / "managed";

    WriteContext(claude_home / "CLAUDE.md", "global instructions");
    WriteContext(claude_home / "rules" / "testing.md", "testing rules");
    WriteContext(project / "CLAUDE.md",
                 "Read @docs/guide.md before work.\n"
                 "`@docs/ignored.md`\n"
                 "``@docs/ignored.md``\n"
                 "```text\n~~~\n@docs/ignored.md\n```\n");
    WriteContext(project / "docs" / "guide.md", "imported guide");
    WriteContext(project / "docs" / "ignored.md", "not an import");
    WriteContext(project / ".claude" / "settings.json", "{}\n");
    WriteContext(managed / "CLAUDE.md", "managed instructions");

    const auto result = project_describer::ContextLoader::LoadClaudeCode(
        project, claude_home, managed);

    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("Managed") / "CLAUDE.md"));
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("Global") / "rules" /
                                         "testing.md"));
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("Project") /
                                         "CLAUDE.md"));
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("docs") / "guide.md"));
    EXPECT_TRUE(!HasContent(result, "not an import"));
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path(".claude") /
                                         "settings.json"));
}

TEST(LoadsCodexSessionsByProjectAndDecodesMessagesAndTools) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / std::filesystem::path(u8"中文项目");
    const auto codex = temp.path() / "codex";
    std::filesystem::create_directories(project);
    WriteContext(codex / "auth.json", "credential must not be loaded");
    WriteContext(codex / "sessions" / "2026" / "10" / "09" / "first.jsonl",
        CodexMetadata(project, "session-a") +
        R"({"type":"event_msg","payload":{"type":"user_message","message":"\u4f60\u597d\nworld \ud83d\ude80"}})" "\n" +
        R"({"type":"response_item","payload":{"type":"message","role":"user","content":[{"type":"input_text","text":"\u4f60\u597d\nworld \ud83d\ude80"}]}})" "\n" +
        R"({"type":"response_item","payload":{"type":"function_call","name":"shell","call_id":"call-1","arguments":"{\"cmd\":\"echo hello\"}"}})" "\n" +
        R"({"type":"response_item","payload":{"type":"function_call_output","call_id":"call-1","output":"hello\n"}})" "\n" +
        R"({"type":"response_item","payload":{"type":"reasoning","summary":[{"type":"summary_text","text":"readable summary"}],"encrypted_content":"opaque-secret-ciphertext"}})" "\n" +
        R"({"type":"compacted","payload":{"message":"compact\nsummary"}})" "\n" +
        R"({"unfinished":)" );
    WriteContext(codex / "archived_sessions" / "second.jsonl",
        CodexMetadata(project, "session-b") +
        R"({"type":"event_msg","payload":{"type":"agent_message","message":"legacy event-only reply"}})" "\n");
    WriteContext(codex / "archived_sessions" / "duplicate.jsonl",
                 CodexMetadata(project, "session-a"));
    WriteContext(codex / "sessions" / "unrelated.jsonl",
                 CodexMetadata(temp.path() / "other-project", "other-session"));

    project_describer::ScanResult result;
    project_describer::AppendCodexSessions(result, project, codex);
    EXPECT_EQ(result.text_files.size(), std::size_t{2});
    EXPECT_EQ(result.scanned_files, std::size_t{2});
    EXPECT_EQ(result.warnings.size(), std::size_t{1});
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path(u8"中文项目") / "session-a.md"));
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path(u8"中文项目") / "session-b.md"));
    for (const auto& file : result.text_files) {
        EXPECT_TRUE(file.content.find("credential must not be loaded") == std::string::npos);
        if (file.file_name == "session-a.md") {
            const auto utf8 = std::u8string(u8"你好\nworld 🚀");
            EXPECT_TRUE(file.content.find(std::string(utf8.begin(), utf8.end())) != std::string::npos);
            EXPECT_TRUE(file.content.find("rules\nsecond line") != std::string::npos);
            EXPECT_TRUE(file.content.find("Tool call: shell") != std::string::npos);
            EXPECT_TRUE(file.content.find("echo hello") != std::string::npos);
            EXPECT_TRUE(file.content.find("Tool result: call-1") != std::string::npos);
            EXPECT_TRUE(file.content.find("compact\nsummary") != std::string::npos);
            EXPECT_TRUE(file.content.find("readable summary") != std::string::npos);
            EXPECT_TRUE(file.content.find("opaque-secret-ciphertext") == std::string::npos);
            EXPECT_TRUE(file.content.find("## user_message") == std::string::npos);
        } else {
            EXPECT_TRUE(file.content.find("legacy event-only reply") != std::string::npos);
        }
    }
}

TEST(LoadsClaudeSessionsFromMetadataWithCustomProjectFolder) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto claude = temp.path() / "claude";
    std::filesystem::create_directories(project);
    const auto transcript = std::string(R"({"type":"mode","mode":"normal","sessionId":"claude-a"})") + "\n" +
        "{\"type\":\"user\",\"cwd\":" + JsonPath(project) +
        R"(,"sessionId":"claude-a","message":{"role":"user","content":"hello\nClaude"}})" "\n" +
        R"({"type":"assistant","message":{"role":"assistant","content":[{"type":"text","text":"answer"},{"type":"tool_use","id":"tool-1","name":"Read","input":{"file_path":"readme.md"}}]}})" "\n" +
        R"({"type":"user","message":{"role":"user","content":[{"type":"tool_result","tool_use_id":"tool-1","content":[{"type":"text","text":"file text"}]}]}})" "\n" +
        R"({"type":"system","subtype":"compact_boundary","content":"compaction boundary"})" "\n";
    WriteContext(claude / "projects" / "custom-folder" / "claude-a.jsonl", transcript);
    WriteContext(claude / "projects" / "custom-folder" / "claude-a" / "subagents" / "agent.jsonl",
                 transcript);
    WriteContext(claude / "projects" / "unrelated" / "other.jsonl",
        "{\"type\":\"user\",\"cwd\":" + JsonPath(temp.path() / "other") +
        R"(,"sessionId":"other","message":{"role":"user","content":"unrelated"}})" "\n");
    WriteContext(claude / ".credentials.json", "credential must not be loaded");
    project_describer::ScanResult result;
    project_describer::AppendClaudeSessions(result, project, claude);
    EXPECT_EQ(result.text_files.size(), std::size_t{1});
    EXPECT_TRUE(result.warnings.empty());
    if (!result.text_files.empty()) {
        const auto& text = result.text_files.front().content;
        EXPECT_TRUE(text.find("hello\nClaude") != std::string::npos);
        EXPECT_TRUE(text.find("Tool call: Read") != std::string::npos);
        EXPECT_TRUE(text.find("file text") != std::string::npos);
        EXPECT_TRUE(text.find("compaction boundary") != std::string::npos);
        EXPECT_TRUE(text.find("unrelated") == std::string::npos);
    }
}

TEST(ContextLoaderIncludesSessionBranch) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex = temp.path() / "codex";
    std::filesystem::create_directories(project / ".git");
    WriteContext(project / "AGENTS.md", "shared instructions");
    WriteContext(codex / "sessions" / "session.jsonl", CodexMetadata(project, "integrated"));
    const auto result = project_describer::ContextLoader::LoadCodex(project, codex);
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("Project") / "AGENTS.md"));
    EXPECT_TRUE(HasPathEnding(result, std::filesystem::path("project") / "integrated.md"));
}

TEST(OversizedSessionRecordsProduceVisibleWarning) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex = temp.path() / "codex";
    std::filesystem::create_directories(project);
    WriteContext(codex / "sessions" / "large.jsonl", CodexMetadata(project, "large") +
        std::string(4 * 1024 * 1024 + 1, 'x') + "\n");
    project_describer::ScanResult result;
    project_describer::AppendCodexSessions(result, project, codex);
    EXPECT_TRUE(result.text_files.empty());
    EXPECT_EQ(result.warnings.size(), std::size_t{1});
}

TEST(CodexFallbackNamesRespectConfigurationOrderAndIgnoreComments) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex = temp.path() / "codex";
    std::filesystem::create_directories(project / ".git");
    WriteContext(codex / "config.toml",
        "# project_doc_fallback_filenames = ['IGNORED.md']\n"
        "project_doc_fallback_filenames = [\n'Z.md', # 'COMMENT.md'\n'A.md']\n");
    WriteContext(project / "Z.md", "first configured fallback");
    WriteContext(project / "A.md", "second configured fallback");
    WriteContext(project / "IGNORED.md", "commented configuration");
    WriteContext(project / "COMMENT.md", "commented name");
    const auto result = project_describer::ContextLoader::LoadCodex(project, codex);
    EXPECT_TRUE(HasContent(result, "first configured fallback"));
    EXPECT_TRUE(!HasContent(result, "second configured fallback"));
    EXPECT_TRUE(!HasContent(result, "commented configuration"));
    EXPECT_TRUE(!HasContent(result, "commented name"));
}

TEST(CodexRejectsFallbackPathsOutsideTheInstructionDirectory) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex = temp.path() / "codex";
    std::filesystem::create_directories(project / ".git");
    WriteContext(codex / "config.toml", "project_doc_fallback_filenames = ['../outside.md']\n");
    WriteContext(temp.path() / "outside.md", "outside instruction scope");
    const auto result = project_describer::ContextLoader::LoadCodex(project, codex);
    EXPECT_TRUE(!HasContent(result, "outside instruction scope"));
}

TEST(ClaudeImportsNestedInstructionsAndBoundsContextFiles) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto claude = temp.path() / "claude";
    WriteContext(project / "nested" / ".claude" / "CLAUDE.md", "@guide.md\n");
    WriteContext(project / "nested" / ".claude" / "guide.md", "nested imported guide");
    WriteContext(project / "nested" / ".claude" / "settings.json", "{\"nested\":true}");
    WriteContext(project / "CLAUDE.md", "@large.md\n");
    WriteContext(project / "large.md", std::string(4 * 1024 * 1024 + 1, 'x'));
    WriteContext(project / ".claude" / ".credentials.json", "credential must stay excluded");
    WriteContext(project / ".claude" / "cache" / "cached.txt", "cache must stay excluded");
    const auto result = project_describer::ContextLoader::LoadClaudeCode(
        project, claude, temp.path() / "managed");
    EXPECT_TRUE(HasContent(result, "nested imported guide"));
    EXPECT_TRUE(HasContent(result, "{\"nested\":true}"));
    EXPECT_TRUE(!HasContent(result, "credential must stay excluded"));
    EXPECT_TRUE(!HasContent(result, "cache must stay excluded"));
    EXPECT_TRUE(!result.warnings.empty());
}

TEST(CodexKeepsEventMessagesForRolesMissingResponseItems) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex = temp.path() / "codex";
    std::filesystem::create_directories(project);
    WriteContext(codex / "sessions" / "mixed.jsonl", CodexMetadata(project, "mixed") +
        R"({"type":"event_msg","payload":{"type":"user_message","message":"earlier question"}})" "\n" +
        R"({"type":"event_msg","payload":{"type":"user_message","message":"question"}})" "\n" +
        R"({"type":"response_item","payload":{"type":"message","role":"user","content":[{"type":"input_text","text":"question"}]}})" "\n" +
        R"({"type":"event_msg","payload":{"type":"user_message","message":"question"}})" "\n" +
        R"({"type":"event_msg","payload":{"type":"agent_message","message":"answer only in event"}})" "\n");
    project_describer::ScanResult result;
    project_describer::AppendCodexSessions(result, project, codex);
    EXPECT_EQ(result.text_files.size(), std::size_t{1});
    if (!result.text_files.empty()) {
        EXPECT_TRUE(result.text_files.front().content.find("answer only in event") != std::string::npos);
        EXPECT_TRUE(result.text_files.front().content.find("earlier question") != std::string::npos);
        const auto& text = result.text_files.front().content;
        const auto first = text.find("## user_message");
        EXPECT_TRUE(first != std::string::npos && text.find("## user_message", first + 1) != std::string::npos);
    }
}

TEST(DeeplyNestedSessionJsonProducesAWarningInsteadOfUnboundedRecursion) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex = temp.path() / "codex";
    std::filesystem::create_directories(project);
    WriteContext(codex / "sessions" / "nested.jsonl", CodexMetadata(project, "nested") +
        "{\"type\":\"unknown\",\"payload\":" + std::string(512, '[') + "0" +
        std::string(512, ']') + "}\n");
    project_describer::ScanResult result;
    project_describer::AppendCodexSessions(result, project, codex);
    EXPECT_TRUE(result.text_files.empty());
    EXPECT_TRUE(!result.warnings.empty());
}

TEST(SessionDisplayNamesRemainUniqueAfterSanitization) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex = temp.path() / "codex";
    std::filesystem::create_directories(project);
    WriteContext(codex / "sessions" / "first.jsonl", CodexMetadata(project, "a/b"));
    WriteContext(codex / "sessions" / "second.jsonl", CodexMetadata(project, "a:b"));
    project_describer::ScanResult result;
    project_describer::AppendCodexSessions(result, project, codex);
    EXPECT_EQ(result.text_files.size(), std::size_t{2});
    if (result.text_files.size() == 2) {
        EXPECT_TRUE(result.text_files[0].relative_path != result.text_files[1].relative_path);
    }
}
