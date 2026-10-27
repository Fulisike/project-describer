# project-describer

`project-describer.exe` is a small Windows-first command-line tool that turns a local
project into one UTF-8 text report. The report has a directory tree and the complete
contents of readable text files, making it practical to paste a concise project context
into a web AI chat.

## Build

Requirements: CMake 3.20 or newer and a C++20 compiler. On Windows, the Visual Studio
2022 C++ workload is the tested setup.

```powershell
cmake -S . -B build -DPROJECT_DESCRIBER_BUILD_TESTS=ON
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

The executable is `build/Debug/project-describer.exe` for the Visual Studio generator.
For a Release CLI build, copy `build/Release/project-describer.exe`. The MSVC
build requires the matching Microsoft Visual C++ Redistributable on the target
computer. GUI distributions also need the deployed Qt DLLs and plugins.

The Qt GUI executable is `build/Debug/project-describer-gui.exe`. Its window and
Windows executable icon are built from `pd.ico`.

For the current workspace, keep the existing `build` configuration (Visual Studio
2022, x64, GUI and tests enabled) and use Release. Configuration is only needed
when changing build options; ordinary edits need only an incremental build:

```powershell
cmake --build build --config Release --target project-describer-gui --parallel 4
```

Core-only validation avoids building or deploying Qt:

```powershell
cmake --build build --config Release --target project-describer project-describer-tests --parallel 4
ctest --test-dir build -C Release -R '^project-describer-tests$' --output-on-failure
```

## Use

```powershell
.\build\Debug\project-describer.exe -p C:\work\my-project
.\build\Debug\project-describer.exe -p C:\work\my-project -o C:\work\reports
```

`-p` is required. `-o` is optional and must name an existing directory. Without `-o`,
the report is written to the project root. The tool chooses `describer.txt`, then
`describer1.txt`, `describer2.txt`, and so on without overwriting a file. Previously
generated `describer*.txt` files in the output directory are excluded from later scans.

Run `project-describer --help` for the command summary.

## GUI file selection and AI context

The GUI separates loading from exporting:

1. Choose a project and one or more sources: project files, Codex context, or
   Claude Code context.
2. Click **Load / Refresh** and expand or collapse the resulting hierarchy.
3. Check only the files that should be included, then click **Export selected files**.

The **每个文件前 N 行** field limits each selected file's exported content to its
first N lines. Empty lines count as lines; files shorter than N lines are included
in full. Leave the field empty or click **默认（全文）** to export all content.

Codex discovery covers layered `AGENTS.md`/`AGENTS.override.md` instructions,
configured fallback instruction names, user and project configuration, local skills,
and local memories. Claude Code discovery covers managed, user, and project
instructions and settings; rules, skills, commands, agents, output styles, project
auto-memory, and recursive `@path` imports. These appear under **Shared context**.

Local conversation transcripts appear under **Sessions**, grouped by local
profile, provider (Codex), selected project, and session ID. Each session is one
readable Markdown document. Select a leaf to preview its decoded messages, tool
calls/results, and recorded compaction information before choosing what to export.
Codex reads `CODEX_HOME/sessions` and `archived_sessions`; Claude Code reads
`CLAUDE_CONFIG_DIR/projects`, with their usual home-directory defaults.
Project matching uses transcript `cwd` metadata, including custom Claude project
folder names. Authentication files, logs, and caches are excluded.

Account/authentication identity is displayed as unknown because local transcripts
do not reliably record it. A readable transcript is not an exact reconstruction of
the current model context: compaction, encrypted reasoning, images, referenced
history, and cloud-only sessions can prevent that. Oversized or malformed records
produce visible warnings. See [the format investigation](docs/session-context-research.md).

Loading and exporting run in the background. Project scanning pipelines directory enumeration
through a bounded 64-job queue to up to four reading/decoding workers, then sorts
the results. Directory links/junctions and file symlinks are not followed. Files
retain the 1 MiB limit, checked again when opened; session records are capped at
4 MiB and complete session input/output at 64 MiB.

See [the performance measurements](docs/scanner-performance.md) for the algorithm,
comparison with the original scanner, and the repeatable benchmark target.

## Text and encoding behavior

Every report is UTF-8. The current Windows implementation recognizes UTF-8 (with or
without BOM), UTF-16 LE/BE (including common no-BOM ASCII-shaped files), UTF-32 with
BOM, and the active Windows ANSI code page. On a Simplified Chinese Windows system the
last category is normally GBK/CP936. Binary-looking files are left out, regardless of
their extension; ordinary text files with no extension are included.

No program can infer every legacy encoding perfectly from arbitrary bytes. The encoding
adapter is intentionally isolated in `src/utils/encoding.*`, so a future GUI build can
offer a user-selected encoding or replace that adapter with ICU4C/Boost.Locale without
changing scanning or report generation.

## Project map

- `src/application`: CLI parsing and use-case orchestration.
- `src/core`: scanning and report rendering/writing.
- `src/model`: small data types shared by the layers.
- `src/utils`: file reading and encoding normalization.
- `third_party/cxxopts`: vendored, header-only CLI parser.
- `tests`: focused unit tests, run through CTest.
- `docs`: architecture rationale and a practical C++ project guide.

See [the architecture notes](docs/architecture.md) and
[the engineering guide](docs/engineering-guide.md).

The [2026-10-26 bug audit](docs/bug-audit-2026-10-26.md) lists confirmed defects,
their fixes, regression results, and verification limits.
