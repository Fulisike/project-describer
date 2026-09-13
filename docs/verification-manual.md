# project-describer 验证手册

本手册用于验证 `project-describer` 核心功能是否满足需求。每次发布或修改核心代码后，请按以下步骤逐项验证，并记录结果。

## 0. 前置条件

| 项目 | 要求 |
|------|------|
| 操作系统 | Windows 10/11（本手册以简体中文版 Windows 为准） |
| 构建工具 | CMake ≥ 3.20 + 支持 C++20 的编译器（VS2022） |
| 目标 | 生成单个可执行文件 `project-describer.exe` |

构建命令：

```bat
cmake -S . -B build -DPROJECT_DESCRIBER_BUILD_TESTS=ON
cmake --build build --config Debug
```

产物路径：`build\Debug\project-describer.exe`

---

## 1. 单元测试验证（自动化）

### 1.1 运行全部测试

```bat
ctest --test-dir build -C Debug --output-on-failure
```

**通过标准**：`100% tests passed`

### 1.2 测试覆盖范围

| 测试文件 | 覆盖内容 |
|---------|---------|
| `tests/test_encoding.cpp` | UTF-8 / UTF-8 BOM / UTF-16 LE·BE / UTF-32 LE·BE / ANSI 检测与转换 |
| `tests/test_scanner.cpp` | 递归遍历、文本过滤、二进制识别、旧报告排除 |
| `tests/test_report_writer.cpp` | 报告渲染、文件名递增、原子写入 |
| `tests/test_command_line.cpp` | `-p` 必需参数、`-o` 可选参数、`--help` |

---

## 2. 命令行功能验证（手动）

### 2.1 帮助信息

```bat
build\Debug\project-describer.exe --help
```

**通过标准**：
- 程序退出码为 0
- 显示用法，包含 `-p/--path` 和 `-o/--output` 说明

### 2.2 缺少 `-p` 参数时报错

```bat
build\Debug\project-describer.exe
```

**通过标准**：
- 程序退出码为非 0
- 错误信息明确指出缺少 `-p/--path`

### 2.3 指定不存在的目录时给出有意义的错误

```bat
build\Debug\project-describer.exe -p C:\nonexistent-dir-xyz
```

**通过标准**：
- 退出码非 0
- 错误信息包含该路径

### 2.4 基本扫描（本项目自身）

```bat
build\Debug\project-describer.exe -p .
```

**通过标准**：
- 退出码 0
- 输出中包含 `## Project Description` 或类似标题
- 目录树中包含 `src/`、`tests/`、`CMakeLists.txt`
- `.git/`、`build/` 目录**不**出现在输出中
- `describer*.txt` 不参与递归

### 2.5 指定输出目录

```bat
mkdir C:\temp\pd-out
build\Debug\project-describer.exe -p . -o C:\temp\pd-out
```

**通过标准**：
- 退出码 0
- `C:\temp\pd-out` 下生成 `describer.txt`
- 无 `describer1.txt`（首次运行）

### 2.6 重名自动递增

再次运行上一条命令：

```bat
build\Debug\project-describer.exe -p . -o C:\temp\pd-out
```

**通过标准**：
- 生成 `describer1.txt`
- 原 `describer.txt` 内容未被覆盖

---

## 3. 编码与中文内容验证（重点）

### 3.1 准备测试项目

创建目录 `C:\temp\enc-test`，放入以下文件：

| 文件 | 编码 | 内容 |
|------|------|------|
| `utf8.txt` | UTF-8（无 BOM） | `你好，项目！` |
| `utf8bom.txt` | UTF-8 带 BOM | `再见，世界！` |
| `gbk.txt` | GBK / CP936 | `中文编码测试` |

> 提示：用记事本「另存为」时可选择编码；GBK 文件在 Windows 简体中文系统上保存为「ANSI」即可。

### 3.2 运行扫描

```bat
build\Debug\project-describer.exe -p C:\temp\enc-test -o C:\temp\enc-test
```

### 3.3 打开报告检查

打开 `C:\temp\enc-test\describer.txt`（用支持 UTF-8 的编辑器）：

**通过标准（3 项全部满足）**：

1. `utf8.txt` 内容显示为 `你好，项目！`，无乱码
2. `utf8bom.txt` 内容显示为 `再见，世界！`，且不出现 `﻿`（U+FEFF 零宽字符）开头
3. `gbk.txt` 内容显示为 `中文编码测试`，无乱码
4. 每个文件的报告片段包含类似：

```
### File: utf8.txt
编码: UTF-8
--- 内容 ---
你好，项目！
```

---

## 4. 特殊文件与边界情况验证

### 4.1 无扩展名文本

创建 `C:\temp\enc-test\LICENSE`（纯文本，无扩展名），重新扫描。

**通过标准**：`LICENSE` 出现在报告中且内容正确。

### 4.2 二进制文件被排除

创建 `C:\temp\enc-test\logo.png`（任意二进制内容），重新扫描。

**通过标准**：`logo.png` 不被包含在报告内容中（在目录树中可出现路径，但不出现在文件内容部分）。

### 4.3 大文件保护

创建 2 MB 的无意义文本（例如文本重复），重新扫描。

**通过标准**：
- 扫描不崩溃
- 报告或警告中说明该文件被跳过（默认上限 1 MiB）

### 4.4 空目录

创建 `C:\temp\enc-test\empty` 空目录并扫描。

**通过标准**：`empty/` 出现在目录树中，扫描不崩溃。

### 4.5 中英文混合文件名

创建 `C:\temp\enc-test\我的文件.cpp` 和 `hello world.md`，重新扫描。

**通过标准**：两个文件都正确出现，中文文件名不显示为乱码。

---

## 5. 回归验证

每次修改核心模块（`scanner`、`encoding`、`report_writer`、`command_line`）后，至少完成：

- [ ] 第 1 节自动化测试全部通过
- [ ] 2.4 基本扫描通过
- [ ] 3.3 编码检查（3 项）通过
- [ ] 4.2 二进制排除通过

---

## 6. 已知限制

- 当前报告输出为 UTF-8 编码的 `.txt` 文件
- 检测到未知/罕见编码时可能回退为 ANSI 转换，存在极低概率偏差
- 二进制判定基于内容特征（NUL 字节、控制字符比例），极端情况下可能有偏差
