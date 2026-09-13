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
For a single distributable executable, build `Release` and copy
`build/Release/project-describer.exe`.

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
