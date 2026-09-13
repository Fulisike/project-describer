# Architecture Notes

## Goal and boundary

The first deliverable is a single CLI executable. It must be useful on its own and must
not make a future GUI rewrite expensive. The GUI is therefore a future caller of the
same application service, not a second implementation of scanning and reporting.

The report contains the project tree, each included file's relative path and file name,
its detected input encoding, and its UTF-8 content. It intentionally does not try to
interpret source code or summarize it: that work belongs to the AI receiving the report.

## Dependency decisions

| Need | Chosen component | Why |
| --- | --- | --- |
| CLI options | [cxxopts 3.3.1](https://github.com/jarro2783/cxxopts) | A small vendored header already present in the repository; it keeps option parsing out of `main`. |
| Recursive paths | C++ standard `std::filesystem` | Cross-platform standard API, no download or wrapper needed. |
| Windows character conversion | Windows NLS `MultiByteToWideChar` / `WideCharToMultiByte` | Mature OS APIs for the configured ANSI code page, including the usual GBK code page on Simplified Chinese Windows. |
| Build and test runner | [CMake](https://cmake.org/cmake/help/latest/) + CTest | Standard portable targets and one command to run tests. |
| Future full locale support | [Boost.Locale](https://www.boost.org/doc/libs/1_90_0/libs/locale/doc/html/charset.html) or ICU4C | These are better choices when explicit, arbitrary legacy code pages are required across platforms. |

The first version avoids a network-time dependency download. Its third-party parser is
checked in; the other selected facilities come from the compiler/runtime already needed
to build the program.

## Layers

```text
main (Windows UTF-16 argv bridge)
  -> application/command_line       parses -p and -o
  -> application/project_describer  validates paths and coordinates work
       -> core/scanner               walks files and creates model data
            -> utils/file_reader     reads bytes and asks for UTF-8 text
                 -> utils/encoding   detects/decodes input bytes
       -> core/report_writer         renders and atomically writes the report
```

`model` is deliberately simple: `FileItem`, `ProjectEntry`, and `TextEncoding` carry
data only. `application` has no byte-decoding logic; `utils` has no knowledge of report
format. That is the useful separation for a future GUI: it can call
`RunProjectDescriber` or the lower-level scanner/writer without copying behavior.

## Operational rules

- Scanning uses content rather than a filename extension allow-list. A text file without
  an extension is therefore kept, while a `.txt` file containing binary bytes is not.
- The report is always written atomically through a temporary file in its destination
  directory.
- Existing generated reports use the reserved name family `describer*.txt`; they are
  omitted when scanning the output directory so repeated runs do not absorb earlier
  reports into the next one.
- An existing output name is never overwritten. The first unused numeric suffix wins.

## Extension points

Add a GUI as a new application adapter that supplies `CommandLineOptions`-equivalent
input and displays progress/results. Add ignore rules or size limits in `Scanner`.
Replace only `EncodingUtils` when a selected code page, ICU4C, or Boost.Locale is needed.
These changes do not need to modify the report writer's format or the file model.
