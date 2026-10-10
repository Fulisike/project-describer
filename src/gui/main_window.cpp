#include "gui/main_window.hpp"

#include <algorithm>
#include <filesystem>
#include <exception>
#include <functional>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtGui/QStandardItem>
#include <QtGui/QIntValidator>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QVBoxLayout>

#include "core/context_loader.hpp"
#include "core/report_writer.hpp"
#include "model/text_encoding.hpp"
#include "utils/logging.hpp"

namespace project_describer::gui {
namespace {

constexpr int kFileIndexRole = Qt::UserRole + 1;

std::filesystem::path PathFromQString(const QString& value) {
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    return std::filesystem::u8path(value.toUtf8().constData());
#endif
}

QString PathToQString(const std::filesystem::path& value) {
    const auto utf8 = value.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(utf8.data()),
                             static_cast<qsizetype>(utf8.size()));
}

int LineCount(const std::string& content) {
    const auto newlines = static_cast<int>(
        std::count(content.begin(), content.end(), '\n'));
    return content.empty() ? 0 : (content.back() == '\n' ? newlines : newlines + 1);
}

void AppendScan(ScanResult& destination, ScanResult source) {
    destination.scanned_files += source.scanned_files;
    destination.text_files.insert(destination.text_files.end(),
                                  std::make_move_iterator(source.text_files.begin()),
                                  std::make_move_iterator(source.text_files.end()));
    destination.warnings.insert(destination.warnings.end(),
                                std::make_move_iterator(source.warnings.begin()),
                                std::make_move_iterator(source.warnings.end()));
}

void AddSelectedEntry(ScanResult& result, const std::filesystem::path& path,
                      std::set<std::string>& added) {
    std::filesystem::path current;
    for (auto it = path.begin(); it != path.end(); ++it) {
        current /= *it;
        const auto utf8 = current.generic_u8string();
        const std::string key(utf8.begin(), utf8.end());
        if (added.insert(key).second) {
            result.entries.push_back({current, std::next(it) != path.end()});
        }
    }
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    SetupUI();
    project_path_edit_->setText(QDir::currentPath());
    output_path_edit_->setText(QDir::currentPath());
    status_label_->setText("请选择上下文来源，然后加载文件树。");
    LOG_INFO("GUI window created");
}

MainWindow::~MainWindow() {
    if (load_thread_ != nullptr) {
        load_thread_->wait();
    }
    LOG_INFO("GUI window destroyed");
}

void MainWindow::SetupUI() {
    setWindowTitle("Project Describer");
    resize(980, 720);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("项目目录：", central));
        project_path_edit_ = new QLineEdit(central);
        row->addWidget(project_path_edit_, 1);
        browse_project_btn_ = new QPushButton("浏览…", central);
        connect(browse_project_btn_, &QPushButton::clicked, this,
                &MainWindow::OnBrowseProject);
        row->addWidget(browse_project_btn_);
        layout->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("输出目录：", central));
        output_path_edit_ = new QLineEdit(central);
        row->addWidget(output_path_edit_, 1);
        browse_output_btn_ = new QPushButton("浏览…", central);
        connect(browse_output_btn_, &QPushButton::clicked, this,
                &MainWindow::OnBrowseOutput);
        row->addWidget(browse_output_btn_);
        layout->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("加载来源：", central));
        project_files_check_ = new QCheckBox("项目文件", central);
        project_files_check_->setChecked(true);
        codex_context_check_ = new QCheckBox("Codex 上下文", central);
        claude_context_check_ = new QCheckBox("Claude Code 上下文", central);
        row->addWidget(project_files_check_);
        row->addWidget(codex_context_check_);
        row->addWidget(claude_context_check_);
        row->addStretch(1);
        load_btn_ = new QPushButton("加载 / 刷新", central);
        connect(load_btn_, &QPushButton::clicked, this, &MainWindow::OnLoad);
        row->addWidget(load_btn_);
        layout->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("勾选需要写入 describer 文本的文件：", central));
        row->addStretch(1);
        select_all_btn_ = new QPushButton("全选", central);
        connect(select_all_btn_, &QPushButton::clicked, this,
                [this] { SetAllChecked(Qt::Checked); });
        row->addWidget(select_all_btn_);
        select_none_btn_ = new QPushButton("全不选", central);
        connect(select_none_btn_, &QPushButton::clicked, this,
                [this] { SetAllChecked(Qt::Unchecked); });
        row->addWidget(select_none_btn_);
        auto* expand_btn = new QPushButton("全部展开", central);
        row->addWidget(expand_btn);
        auto* collapse_btn = new QPushButton("全部收起", central);
        row->addWidget(collapse_btn);
        layout->addLayout(row);

        tree_view_ = new QTreeView(central);
        connect(expand_btn, &QPushButton::clicked, tree_view_, &QTreeView::expandAll);
        connect(collapse_btn, &QPushButton::clicked, tree_view_, &QTreeView::collapseAll);
    }
    {
        tree_view_->setParent(central);
        tree_model_ = new QStandardItemModel(this);
        tree_model_->setHorizontalHeaderLabels({"文件树"});
        tree_view_->setModel(tree_model_);
        tree_view_->setAlternatingRowColors(true);
        tree_view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        connect(tree_model_, &QStandardItemModel::itemChanged, this,
                &MainWindow::OnTreeItemChanged);
        layout->addWidget(tree_view_, 3);
    }
    {
        preview_edit_ = new QTextEdit(central);
        preview_edit_->setReadOnly(true);
        preview_edit_->setPlaceholderText("加载和导出结果会显示在这里。");
        preview_edit_->setMaximumHeight(150);
        layout->addWidget(preview_edit_, 1);
        connect(tree_view_->selectionModel(), &QItemSelectionModel::currentChanged,
                this, [this](const QModelIndex& current) {
            const auto index = current.data(kFileIndexRole);
            if (index.isValid() && index.toULongLong() < loaded_scan_.text_files.size()) {
                const auto& file = loaded_scan_.text_files[static_cast<std::size_t>(index.toULongLong())];
                preview_edit_->setPlainText(QString::fromUtf8(file.content.data(),
                    static_cast<qsizetype>(file.content.size())));
            }
        });
    }
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("每个文件前 N 行：", central));
        file_line_limit_edit_ = new QLineEdit(central);
        file_line_limit_edit_->setObjectName("file_line_limit");
        file_line_limit_edit_->setPlaceholderText("默认：全文");
        file_line_limit_edit_->setMaximumWidth(130);
        file_line_limit_edit_->setValidator(new QIntValidator(
            1, (std::numeric_limits<int>::max)(), file_line_limit_edit_));
        row->addWidget(file_line_limit_edit_);
        reset_line_limit_btn_ = new QPushButton("默认（全文）", central);
        connect(reset_line_limit_btn_, &QPushButton::clicked,
                file_line_limit_edit_, &QLineEdit::clear);
        row->addWidget(reset_line_limit_btn_);
        row->addStretch(1);
        layout->addLayout(row);
    }
    {
        auto* row = new QHBoxLayout;
        status_label_ = new QLabel(central);
        row->addWidget(status_label_, 1);
        progress_bar_ = new QProgressBar(central);
        progress_bar_->setRange(0, 0);
        progress_bar_->setFixedWidth(160);
        progress_bar_->hide();
        row->addWidget(progress_bar_);
        start_btn_ = new QPushButton("导出所选文件", central);
        start_btn_->setEnabled(false);
        connect(start_btn_, &QPushButton::clicked, this, &MainWindow::OnStart);
        row->addWidget(start_btn_);
        layout->addLayout(row);
    }

    setCentralWidget(central);
}

void MainWindow::OnBrowseProject() {
    try {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择项目目录", project_path_edit_->text());
        if (!dir.isEmpty()) {
            project_path_edit_->setText(QDir::toNativeSeparators(dir));
            loaded_project_path_.clear();
            start_btn_->setEnabled(false);
            status_label_->setText("项目目录已更改，请重新加载文件树。");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error in folder dialog: {}", e.what());
        QMessageBox::critical(this, "错误", "无法打开目录选择器：\n" +
                                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::OnBrowseOutput() {
    try {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择输出目录", output_path_edit_->text());
        if (!dir.isEmpty()) {
            output_path_edit_->setText(QDir::toNativeSeparators(dir));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error in folder dialog: {}", e.what());
        QMessageBox::critical(this, "错误", "无法打开目录选择器：\n" +
                                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::OnLoad() {
    if (project_path_edit_->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "缺少项目目录", "请先选择项目目录。");
        return;
    }
    if (!project_files_check_->isChecked() && !codex_context_check_->isChecked() &&
        !claude_context_check_->isChecked()) {
        QMessageBox::warning(this, "缺少加载来源", "请至少选择一种加载来源。");
        return;
    }

    const auto project_root = PathFromQString(project_path_edit_->text());
    std::error_code error;
    if (!std::filesystem::is_directory(project_root, error) || error) {
        QMessageBox::warning(this, "无效项目目录", "项目目录不存在或不可读取。");
        return;
    }

    SetBusy(true);
    status_label_->setText("正在加载文件树…");
    const auto project_path = QDir::cleanPath(project_path_edit_->text());
    const auto output_dir = PathFromQString(output_path_edit_->text());
    const bool include_project = project_files_check_->isChecked();
    const bool include_codex = codex_context_check_->isChecked();
    const bool include_claude = claude_context_check_->isChecked();
    struct LoadState {
        ScanResult scan;
        std::exception_ptr error;
    };
    const auto state = std::make_shared<LoadState>();
    load_thread_ = QThread::create([state, project_root, output_dir,
                                   include_project, include_codex, include_claude] {
        try {
            if (include_project) {
                AppendScan(state->scan, Scanner::Scan(project_root, output_dir));
            }
            if (include_codex) {
                AppendScan(state->scan, ContextLoader::LoadCodex(project_root));
            }
            if (include_claude) {
                AppendScan(state->scan, ContextLoader::LoadClaudeCode(project_root));
            }
        } catch (...) {
            state->error = std::current_exception();
        }
    });
    load_thread_->setParent(this);
    connect(load_thread_, &QThread::finished, this, [this, state, project_path] {
        load_thread_ = nullptr;
        try {
            if (state->error) {
                std::rethrow_exception(state->error);
            }
            loaded_scan_ = std::move(state->scan);
            loaded_project_path_ = project_path;
            PopulateTree();
            start_btn_->setEnabled(!loaded_scan_.text_files.empty());

            std::ostringstream summary;
            summary << "Loaded " << loaded_scan_.text_files.size()
                    << " selectable text files.\n"
                    << "Expand folders and uncheck anything that should not be exported.";
            if (!loaded_scan_.warnings.empty()) {
                summary << "\nWarnings: " << loaded_scan_.warnings.size()
                        << " (see describer.log).";
                for (const auto& warning : loaded_scan_.warnings) {
                    summary << "\n- " << warning;
                    LOG_WARN("{}", warning);
                }
            }
            preview_edit_->setPlainText(QString::fromStdString(summary.str()));
            status_label_->setText(QString("已加载 %1 个可选文件。").arg(
                static_cast<qulonglong>(loaded_scan_.text_files.size())));
            LOG_INFO("Loaded {} selectable files", loaded_scan_.text_files.size());
        } catch (const std::exception& e) {
            LOG_ERROR("Context load failed: {}", e.what());
            loaded_project_path_.clear();
            start_btn_->setEnabled(false);
            status_label_->setText("加载失败，请查看日志。");
            QMessageBox::critical(this, "加载失败", QString::fromUtf8(e.what()));
        }
        SetBusy(false);
    });
    connect(load_thread_, &QThread::finished, load_thread_, &QObject::deleteLater);
    load_thread_->start();
}

void MainWindow::PopulateTree() {
    updating_checks_ = true;
    tree_model_->clear();
    tree_model_->setHorizontalHeaderLabels({"文件树"});

    QHash<QString, QStandardItem*> directories;
    auto find_or_add_child = [&directories](QStandardItem* parent, const QString& name,
                                            const QString& key) {
        if (const auto found = directories.constFind(key); found != directories.cend()) {
            return *found;
        }
        auto* item = new QStandardItem(name);
        item->setEditable(false);
        item->setCheckable(true);
        item->setCheckState(Qt::Checked);
        parent->appendRow(item);
        directories.insert(key, item);
        return item;
    };

    for (std::size_t index = 0; index < loaded_scan_.text_files.size(); ++index) {
        const auto& file = loaded_scan_.text_files[index];
        std::vector<std::filesystem::path> components(file.relative_path.begin(),
                                                      file.relative_path.end());
        if (components.empty()) {
            continue;
        }

        QStandardItem* parent = tree_model_->invisibleRootItem();
        QString directory_key;
        const auto first = components.front().string();
        if (first != "Codex" && first != "Claude Code") {
            directory_key = "项目文件";
            parent = find_or_add_child(parent, "项目文件", directory_key);
        }
        for (std::size_t part = 0; part < components.size(); ++part) {
            const bool is_leaf = part + 1 == components.size();
            const QString name = PathToQString(components[part]);
            if (!is_leaf) {
                directory_key += '/' + name;
                parent = find_or_add_child(parent, name, directory_key);
                continue;
            }
            auto* item = new QStandardItem(name);
            item->setEditable(false);
            item->setCheckable(true);
            item->setCheckState(Qt::Checked);
            item->setData(QVariant::fromValue(static_cast<qulonglong>(index)),
                          kFileIndexRole);
            item->setToolTip(QString("%1\n%2 行 · %3")
                                 .arg(PathToQString(file.relative_path))
                                 .arg(LineCount(file.content))
                                 .arg(QString::fromStdString(
                                     std::string(EncodingName(file.source_encoding)))));
            parent->appendRow(item);
        }
    }

    updating_checks_ = false;
    tree_view_->expandToDepth(1);
}

void MainWindow::OnTreeItemChanged(QStandardItem* item) {
    if (updating_checks_ || item == nullptr) {
        return;
    }
    updating_checks_ = true;

    if (item->hasChildren() && item->checkState() != Qt::PartiallyChecked) {
        std::function<void(QStandardItem*)> set_children = [&](QStandardItem* parent) {
            for (int row = 0; row < parent->rowCount(); ++row) {
                auto* child = parent->child(row);
                child->setCheckState(parent->checkState());
                set_children(child);
            }
        };
        set_children(item);
    }

    for (auto* parent = item->parent(); parent != nullptr; parent = parent->parent()) {
        int checked = 0;
        int unchecked = 0;
        for (int row = 0; row < parent->rowCount(); ++row) {
            const auto state = parent->child(row)->checkState();
            checked += state == Qt::Checked;
            unchecked += state == Qt::Unchecked;
        }
        if (checked == parent->rowCount()) {
            parent->setCheckState(Qt::Checked);
        } else if (unchecked == parent->rowCount()) {
            parent->setCheckState(Qt::Unchecked);
        } else {
            parent->setCheckState(Qt::PartiallyChecked);
        }
    }
    updating_checks_ = false;
}

void MainWindow::SetAllChecked(Qt::CheckState state) {
    updating_checks_ = true;
    std::function<void(QStandardItem*)> set_state = [&](QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            auto* child = parent->child(row);
            child->setCheckState(state);
            set_state(child);
        }
    };
    set_state(tree_model_->invisibleRootItem());
    updating_checks_ = false;
}

void MainWindow::OnStart() {
    if (loaded_project_path_.isEmpty() ||
        loaded_project_path_ != QDir::cleanPath(project_path_edit_->text())) {
        QMessageBox::warning(this, "需要重新加载", "项目目录已变化，请先重新加载文件树。");
        return;
    }

    std::size_t max_lines_per_file = 0;
    const auto line_limit = file_line_limit_edit_->text().trimmed();
    if (!line_limit.isEmpty()) {
        bool valid = false;
        const int lines = line_limit.toInt(&valid);
        if (!valid || lines < 1) {
            QMessageBox::warning(this, "无效行数", "请输入正整数，或选择“默认（全文）”。");
            return;
        }
        max_lines_per_file = static_cast<std::size_t>(lines);
    }

    std::vector<std::size_t> selected_indices;
    std::function<void(QStandardItem*)> collect = [&](QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            auto* child = parent->child(row);
            const auto value = child->data(kFileIndexRole);
            if (value.isValid() && child->checkState() == Qt::Checked) {
                selected_indices.push_back(static_cast<std::size_t>(value.toULongLong()));
            }
            collect(child);
        }
    };
    collect(tree_model_->invisibleRootItem());
    if (selected_indices.empty()) {
        QMessageBox::warning(this, "未选择文件", "请至少勾选一个要导出的文件。");
        return;
    }

    const auto output_dir = PathFromQString(output_path_edit_->text());
    std::error_code error;
    if (!std::filesystem::is_directory(output_dir, error) || error) {
        QMessageBox::warning(this, "无效输出目录", "输出目录不存在或不可写。");
        return;
    }

    SetBusy(true);
    status_label_->setText("正在生成 describer 文本…");
    QApplication::processEvents();

    try {
        ScanResult selected;
        selected.scanned_files = selected_indices.size();
        selected.warnings = loaded_scan_.warnings;
        std::set<std::string> added_entries;
        for (const auto index : selected_indices) {
            if (index >= loaded_scan_.text_files.size()) {
                continue;
            }
            selected.text_files.push_back(loaded_scan_.text_files[index]);
            AddSelectedEntry(selected, loaded_scan_.text_files[index].relative_path,
                             added_entries);
        }

        const auto project_root = PathFromQString(project_path_edit_->text());
        const auto report = ReportWriter::Render(project_root, selected, max_lines_per_file);
        const auto output_path = ReportWriter::ChooseOutputPath(output_dir);
        std::string write_error;
        if (!ReportWriter::WriteUtf8(output_path, report, write_error)) {
            throw std::runtime_error(write_error);
        }

        preview_edit_->setPlainText(
            QString("已导出 %1 个文件。\n%2")
                .arg(static_cast<qulonglong>(selected.text_files.size()))
                .arg(PathToQString(output_path)));
        status_label_->setText("导出完成。");
        LOG_INFO("Exported {} selected files to {}", selected.text_files.size(),
                 output_path.string());
    } catch (const std::exception& e) {
        LOG_ERROR("Export failed: {}", e.what());
        status_label_->setText("导出失败，请查看日志。");
        QMessageBox::critical(this, "导出失败", QString::fromUtf8(e.what()));
    }

    SetBusy(false);
}

void MainWindow::SetBusy(bool busy) {
    load_btn_->setEnabled(!busy);
    start_btn_->setEnabled(!busy && !loaded_project_path_.isEmpty() &&
                           !loaded_scan_.text_files.empty());
    browse_project_btn_->setEnabled(!busy);
    browse_output_btn_->setEnabled(!busy);
    project_path_edit_->setEnabled(!busy);
    output_path_edit_->setEnabled(!busy);
    file_line_limit_edit_->setEnabled(!busy);
    reset_line_limit_btn_->setEnabled(!busy);
    tree_view_->setEnabled(!busy);
    project_files_check_->setEnabled(!busy);
    codex_context_check_->setEnabled(!busy);
    claude_context_check_->setEnabled(!busy);
    select_all_btn_->setEnabled(!busy);
    select_none_btn_->setEnabled(!busy);
    if (busy) {
        progress_bar_->setRange(0, 0);
        progress_bar_->show();
    } else {
        progress_bar_->hide();
    }
}

}  // namespace project_describer::gui
