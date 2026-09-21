#include "test_framework.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "core/context_loader.hpp"

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

}  // namespace

TEST(LoadsCodexContextUsingOverrideAndConfiguredFallback) {
    ScopedContextDirectory temp;
    const auto project = temp.path() / "project";
    const auto codex_home = temp.path() / "codex";

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
                 "```text\n@docs/ignored.md\n```\n");
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
