# Scanner performance and verification

Implementation and investigation date: 2026-10-09.

The scanner keeps one directory enumerator and sends eligible files through a
64-job queue to independent readers. Each reader opens and decodes its file and
owns its result buffer. After joining all readers, the caller merges and sorts
paths and warnings. The default is 2–4 readers according to reported hardware;
`ScannerConfig::worker_count = 1` executes synchronously, and explicit counts are
bounded at 8. This overlaps I/O and decoding without a thread per file.

The old scanner performed two canonical path queries for every file when checking
generated reports. Now it checks the report filename first, resolves the output
once, and queries a canonical parent only for report candidates requiring alias
resolution. FileReader determines size from the opened stream, removing its
separate filesystem size query and checking the configured limit before allocating.
Decoded content is moved into the result instead of copied.

Windows junctions/reparse directories and file symlinks are skipped. Special files
are not read. Permission-skipping directory enumeration is retained. Workers catch
exceptions, wake a blocked producer, and join on every exit path. Tests compare
serial and parallel paths, file contents, encodings, counts, and warnings across
more jobs than fit in the queue, including empty/Chinese/UTF-16 files, binary data,
oversized and invalid files, ignored directories, and report exclusion.

The GUI also loads in a QThread and updates widgets through its completion signal.
Folder lookup uses a hash table when constructing the tree, avoiding repeated
searches through sibling rows. Selecting a leaf previews the file/session text.

## Measurements

MSVC 19.41, x64 Release, on this Windows machine. One shared fixture contained
4,096 UTF-8 files of 8,192 bytes each, in 128 directories (32 MiB total). Each
configuration ran once to warm up and five measured times. Only `Scanner::Scan`
was timed; fixture creation and report export were excluded.

The original scanner and FileReader were built from Git commit `bdec5a4` and ran
against the exact same fixture as the updated implementation.

| Implementation | Median ms | Minimum ms | Maximum ms |
|---|---:|---:|---:|
| Original serial scanner | 1,606.03 | 1,500.83 | 1,660.66 |
| Updated, 1 reader | 504.905 | 482.160 | 616.855 |
| Updated, 2 readers | 312.113 | 291.042 | 316.023 |
| Updated, 4 readers | 300.948 | 299.709 | 306.574 |
| Updated, 8 readers | 325.677 | 323.091 | 326.858 |

Four readers gave about 5.3× the original throughput (81% less time), and 1.68×
the updated synchronous scanner. The metadata optimization accounts for much of
the total gain. Eight readers were slower here, so the automatic cap is four.
These are warm-cache measurements for this fixture, not a universal speed promise.
Disk, antivirus, directory shape, and encoding mix affect results; a slow HDD may
benefit from a lower count.

## Repeating the benchmark

The benchmark is excluded from normal builds:

```powershell
cmake --build build --config Release --target project-describer-scan-benchmark --parallel 4
.\build\Release\project-describer-scan-benchmark.exe
# An existing fixture can be shared by each reader-count comparison:
.\build\Release\project-describer-scan-benchmark.exe C:\work\fixture 1
.\build\Release\project-describer-scan-benchmark.exe C:\work\fixture 4
```

Without a path, it creates and removes its own unique temporary fixture. With a
path, it only scans that directory. The first run is excluded from the reported
median; five samples remain.

## Design sources

- [ripgrep's parallel walker](https://github.com/BurntSushi/ripgrep/blob/master/crates/ignore/src/walk.rs)
  demonstrates limiting workers and keeping per-worker traversal state. Our smaller
  pipeline applies bounded workers specifically to this scanner's read/decode work.
- [Microsoft C++ thread safety](https://learn.microsoft.com/en-us/cpp/standard-library/thread-safety-in-the-cpp-standard-library)
  supports separate per-thread objects; the shared queue is synchronized.
- [Windows reparse points](https://learn.microsoft.com/en-us/windows/win32/fileio/reparse-points)
  explains why junctions need protection beyond ordinary directory recursion.
- [Qt threading rules](https://doc.qt.io/qt-6/threads-qobject.html) and
  [QThread lifecycle](https://doc.qt.io/qt-6/qthread.html) support background work,
  queued completion, and joining before destruction.

Native Windows enumeration with `FIND_FIRST_EX_LARGE_FETCH` was investigated via
[Microsoft's API documentation](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstfileexw).
It would introduce a platform-specific walker and is unnecessary for the measured
bottleneck; the implementation retains standard filesystem enumeration.
