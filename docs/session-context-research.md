# Local Codex and Claude Code session context

Investigated on 2026-10-09 using official documentation, public implementation
sources, and local record schemas. Local message bodies and credential files were
not copied into this document.

## What exists locally

| Application | Local transcript location | Format and project association |
|---|---|---|
| Codex | `$CODEX_HOME/sessions/YYYY/MM/DD/rollout-*.jsonl`, also `archived_sessions` | UTF-8 JSONL; `session_meta.payload` records ID, cwd, origin/source, provider, and sometimes base instructions |
| Claude Code | `$CLAUDE_CONFIG_DIR/projects/<project>/<session-id>.jsonl` | UTF-8 JSONL; message/metadata records contain `sessionId` and cwd; folders may be encoded paths or custom names |

Default homes are `~/.codex` and `~/.claude`. Codex's
[official rollout recorder](https://github.com/openai/codex/blob/main/codex-rs/rollout/src/recorder.rs)
persists JSONL, and its
[rollout library](https://github.com/openai/codex/blob/main/codex-rs/rollout/src/lib.rs)
defines session and archive storage. The
[upstream discussion](https://github.com/openai/codex/discussions/12668) also shows
the session_meta / turn_context / response_item structure. The locally inspected
Codex schemas contained timestamp, ordinal, type, and payload, with session ID,
cwd, source, model_provider, and base_instructions in metadata.

Claude's [official session documentation](https://code.claude.com/docs/en/sessions#where-transcripts-are-stored)
confirms local JSONL, storage overrides, custom project directory names, and the
fact that its internal entry format changes between versions. Its
[directory reference](https://code.claude.com/docs/en/claude-directory#application-data)
also documents parent transcripts, subagent transcripts, and spilled tool results.
Local schemas included user/assistant messages with string or array content,
tool_use/tool_result blocks, system records, and metadata-only entries before the
first message.

## Grouping and readable output

The application shows:

```text
Codex or Claude Code
├── Shared context
│   └── existing instructions, settings, skills, memories
└── Sessions
    └── Local profile (auth unknown)
        └── Provider - openai       [Codex only; a provider is not an account]
            └── selected-project
                └── session-id.md
```

Only transcripts whose metadata cwd matches the selected directory are loaded.
The matcher normalizes paths and handles Windows ASCII case differences; it does
not infer the project solely from Claude's lossy folder name. Session IDs deduplicate
copies across active/archive locations. Each readable document identifies its
source path and local profile. Claude subagent directories are excluded from the
parent-session list.

Messages decode JSON escapes, Unicode, surrogate pairs, and embedded tool argument
JSON. Tool IDs are retained to associate calls and results. Codex event messages
are used as a fallback for older transcripts without response-item messages, so
normal event/response duplicates do not repeat the conversation. Recorded base
instructions, turn settings, compaction records, and unknown record types are
retained as text or formatted JSON. Ciphertext is labeled as unavailable, and image
blocks are labeled instead of dumping binary data.

The JSON parser is the vendored official nlohmann/json 3.12.0 single header with its
MIT license. Files are read within a captured size budget; an actively appended
incomplete JSONL tail is skipped with a visible warning. Individual lines have a
4 MiB cap and session input/rendered output a 64 MiB cap. Oversized sessions are
skipped with a warning rather than silently exported partially.

## Limits of the evidence

Neither the inspected metadata nor the documented transcript layout establishes
a dependable account/authentication partition. Switching credentials today does
not prove which account created an older transcript. The loader therefore groups
by the configured local profile and explicitly marks authentication as unknown.
It does not open auth.json, .credentials.json, or credential databases.

This export is readable saved session history. It cannot promise an exact model
input snapshot after every turn: context compaction can change the effective
history, encrypted reasoning cannot be decoded locally, and images or externally
referenced/spilled records may not be present inline. Current instruction files
are shared context, not proof of their historical contents in a past session.
Compressed archive files, referenced parent-history reconstruction, and cloud-only
sessions are outside this initial JSONL implementation.

Validation covers project filtering, custom Claude folder names, session-ID
deduplication, archives, old event-only Codex records, tool calls/results, Unicode
and line escapes, compaction, incomplete records, and oversized records. A Qt test
target additionally exercises background loading, tree leaves, decoded preview,
and selection-based export; it can be built with `project-describer-gui-tests`.
