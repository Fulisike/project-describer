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

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QSettings>
#include <QtGui/QStandardItem>
#include <QtGui/QIntValidator>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QSplitter>
#include <QtWidgets/QVBoxLayout>

#include "core/context_loader.hpp"
#include "core/report_writer.hpp"
#include "model/text_encoding.hpp"
#include "utils/logging.hpp"
#include "utils/path_utils.hpp"

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
    int lines = 0;
    for (std::size_t index = 0; index < content.size(); ++index) {
        if (content[index] == '\r' || content[index] == '\n') {
            ++lines;
            if (content[index] == '\r' && index + 1 < content.size() &&
                content[index + 1] == '\n') ++index;
        }
    }
    return content.empty() ? 0 :
        ((content.back() == '\r' || content.back() == '\n') ? lines : lines + 1);
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
    QSettings settings;
    const auto default_path = QCoreApplication::applicationDirPath();
    project_path_edit_->setText(QDir::toNativeSeparators(
        settings.value("paths/project", default_path).toString()));
    output_path_edit_->setText(QDir::toNativeSeparators(
        settings.value("paths/output", default_path).toString()));
    status_label_->setText("请选择上下文来源，然后加载文件树。");
    LOG_INFO("GUI window created");
}

MainWindow::~MainWindow() {
    if (worker_thread_ != nullptr) {
        worker_thread_->wait();
    }
    if (preview_thread_ != nullptr) {
        preview_thread_->wait();
    }
    QSettings settings;
    const auto project_path = project_path_edit_->text().trimmed();
    const auto output_path = output_path_edit_->text().trimmed();
    if (!project_path.isEmpty()) {
        settings.setValue("paths/project", QDir::toNativeSeparators(QDir(project_path).absolutePath()));
    }
    if (!output_path.isEmpty()) {
        settings.setValue("paths/output", QDir::toNativeSeparators(QDir(output_path).absolutePath()));
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
        auto* splitter = new QSplitter(Qt::Horizontal, central);
        splitter->setObjectName("preview_splitter");
        auto* file_pane = new QWidget(splitter);
        auto* file_layout = new QVBoxLayout(file_pane);
        file_layout->setContentsMargins(0, 0, 0, 0);
        file_layout->addWidget(new QLabel("当前文件预览", file_pane));
        file_preview_edit_ = new QPlainTextEdit(file_pane);
        file_preview_edit_->setObjectName("file_preview");
        file_preview_edit_->setReadOnly(true);
        file_preview_edit_->setPlaceholderText("请从上方列表中选择一个文件。");
        file_layout->addWidget(file_preview_edit_);
        auto* export_pane = new QWidget(splitter);
        auto* export_layout = new QVBoxLayout(export_pane);
        export_layout->setContentsMargins(0, 0, 0, 0);
        export_layout->addWidget(new QLabel("导出内容预览", export_pane));
        export_preview_edit_ = new QPlainTextEdit(export_pane);
        export_preview_edit_->setObjectName("export_preview");
        export_preview_edit_->setReadOnly(true);
        export_preview_edit_->setPlaceholderText("请先加载文件树，再勾选需要导出的文件。");
        export_layout->addWidget(export_preview_edit_);
        splitter->setStretchFactor(0, 1);
        splitter->setStretchFactor(1, 1);
        splitter->setMaximumHeight(200);
        layout->addWidget(splitter, 1);
        connect(tree_view_->selectionModel(), &QItemSelectionModel::currentChanged,
                this, &MainWindow::UpdateFilePreview);
        connect(tree_view_->selectionModel(), &QItemSelectionModel::selectionChanged,
                this, &MainWindow::UpdateFilePreview);
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
    preview_timer_ = new QTimer(this);
    preview_timer_->setSingleShot(true);
    preview_timer_->setInterval(120);
    connect(preview_timer_, &QTimer::timeout, this, &MainWindow::UpdateExportPreview);
    connect(file_line_limit_edit_, &QLineEdit::textChanged, this, &MainWindow::ScheduleExportPreview);
    const auto invalidate = [this] {
        loaded_project_path_.clear();
        tree_view_->selectionModel()->clear();
        ScheduleExportPreview();
        status_label_->setText("加载条件已更改，请重新加载文件树。");
    };
    connect(project_path_edit_, &QLineEdit::textChanged, this, invalidate);
    connect(output_path_edit_, &QLineEdit::textChanged, this, invalidate);
    for (auto* checkbox : {project_files_check_, codex_context_check_, claude_context_check_}) {
        connect(checkbox, &QCheckBox::toggled, this, invalidate);
    }
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
    loaded_project_path_.clear();
    tree_view_->selectionModel()->clear();
    ScheduleExportPreview();
    status_label_->setText("正在加载文件树…");
    const auto project_path = QDir::cleanPath(project_path_edit_->text());
    const auto output_dir = PathFromQString(output_path_edit_->text());
    const bool include_project = project_files_check_->isChecked();
    const bool include_codex = codex_context_check_->isChecked();
    const bool include_claude = claude_context_check_->isChecked();
    struct LoadState {
        ScanResult scan;
        std::size_t project_files = 0;
        std::exception_ptr error;
    };
    const auto state = std::make_shared<LoadState>();
    worker_thread_ = QThread::create([state, project_root, output_dir,
                                   include_project, include_codex, include_claude] {
        try {
            if (include_project) {
                AppendScan(state->scan, Scanner::Scan(project_root, output_dir));
                state->project_files = state->scan.text_files.size();
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
    worker_thread_->setParent(this);
    connect(worker_thread_, &QThread::finished, this, [this, state, project_path] {
        worker_thread_ = nullptr;
        try {
            if (state->error) {
                std::rethrow_exception(state->error);
            }
            loaded_scan_ = std::move(state->scan);
            loaded_project_file_count_ = state->project_files;
            loaded_project_path_ = project_path;
            PopulateTree();
            ScheduleExportPreview();

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
            status_label_->setToolTip(QString::fromStdString(summary.str()));
            status_label_->setText(QString("已加载 %1 个可选文件。").arg(
                static_cast<qulonglong>(loaded_scan_.text_files.size())));
            if (!loaded_scan_.warnings.empty()) {
                status_label_->setText(status_label_->text() + QString(" %1 条警告，详情见提示和日志。")
                    .arg(static_cast<qulonglong>(loaded_scan_.warnings.size())));
            }
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
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);
    worker_thread_->start();
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
        if (index < loaded_project_file_count_) {
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
    ScheduleExportPreview();
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
    ScheduleExportPreview();
}

void MainWindow::UpdateFilePreview() {
    file_preview_edit_->clear();
    const auto index = tree_view_->currentIndex().data(kFileIndexRole);
    if (tree_view_->selectionModel()->hasSelection() && index.isValid() &&
        index.toULongLong() < loaded_scan_.text_files.size()) {
        const auto& file = loaded_scan_.text_files[static_cast<std::size_t>(index.toULongLong())];
        file_preview_edit_->setPlainText(QString::fromUtf8(file.content.data(),
            static_cast<qsizetype>(file.content.size())));
    }
}

ScanResult MainWindow::SelectedScan() const {
    ScanResult selected;
    selected.warnings = loaded_scan_.warnings;
    std::set<std::string> added_entries;
    std::function<void(QStandardItem*)> collect = [&](QStandardItem* parent) {
        for (int row = 0; row < parent->rowCount(); ++row) {
            auto* child = parent->child(row);
            const auto value = child->data(kFileIndexRole);
            if (value.isValid() && child->checkState() == Qt::Checked) {
                const auto& file = loaded_scan_.text_files[static_cast<std::size_t>(value.toULongLong())];
                selected.text_files.push_back(file);
                AddSelectedEntry(selected, file.relative_path, added_entries);
            }
            collect(child);
        }
    };
    collect(tree_model_->invisibleRootItem());
    selected.scanned_files = selected.text_files.size();
    return selected;
}

void MainWindow::ScheduleExportPreview() {
    ++preview_revision_;
    export_report_.reset();
    start_btn_->setEnabled(false);
    export_preview_edit_->clear();
    preview_timer_->stop();
    if (loaded_project_path_.isEmpty()) {
        export_preview_edit_->setPlaceholderText("请先加载文件树，再勾选需要导出的文件。");
        return;
    }
    export_preview_edit_->setPlaceholderText("正在生成导出预览…");
    preview_timer_->start();
}

void MainWindow::UpdateExportPreview() {
    if (preview_thread_ != nullptr || loaded_project_path_.isEmpty()) {
        return;
    }
    std::size_t max_lines = 0;
    const auto line_limit = file_line_limit_edit_->text().trimmed();
    if (!line_limit.isEmpty()) {
        bool valid = false;
        const auto lines = line_limit.toInt(&valid);
        if (!valid || lines < 1) {
            export_preview_edit_->setPlaceholderText("请输入正整数行数，或选择“默认（全文）”。");
            return;
        }
        max_lines = static_cast<std::size_t>(lines);
    }
    auto selected = SelectedScan();
    if (selected.text_files.empty()) {
        export_preview_edit_->setPlaceholderText("请从上方列表中勾选需要导出的文件。");
        return;
    }
    const auto project_root = PathFromQString(loaded_project_path_);
    const auto revision = preview_revision_;
    struct PreviewState {
        std::shared_ptr<const std::string> report;
        std::size_t file_count;
        std::exception_ptr error;
    };
    const auto state = std::make_shared<PreviewState>();
    state->file_count = selected.text_files.size();
    preview_thread_ = QThread::create([state, project_root, max_lines, selected = std::move(selected)] {
        try {
            state->report = std::make_shared<const std::string>(
                ReportWriter::Render(project_root, selected, max_lines));
        } catch (...) {
            state->error = std::current_exception();
        }
    });
    preview_thread_->setParent(this);
    connect(preview_thread_, &QThread::finished, this, [this, state, revision] {
        preview_thread_ = nullptr;
        if (revision != preview_revision_) {
            if (!loaded_project_path_.isEmpty()) preview_timer_->start();
            return;
        }
        try {
            if (state->error) std::rethrow_exception(state->error);
            export_report_ = state->report;
            export_file_count_ = state->file_count;
            export_preview_edit_->setPlainText(QString::fromUtf8(
                export_report_->data(), static_cast<qsizetype>(export_report_->size())));
            start_btn_->setEnabled(worker_thread_ == nullptr);
        } catch (const std::exception& e) {
            LOG_ERROR("Export preview failed: {}", e.what());
            export_preview_edit_->setPlaceholderText("导出预览生成失败，请查看日志。");
        }
    });
    connect(preview_thread_, &QThread::finished, preview_thread_, &QObject::deleteLater);
    preview_thread_->start();
}

void MainWindow::OnStart() {
    if (loaded_project_path_.isEmpty() ||
        loaded_project_path_ != QDir::cleanPath(project_path_edit_->text())) {
        QMessageBox::warning(this, "需要重新加载", "项目目录已变化，请先重新加载文件树。");
        return;
    }

    if (!export_report_) {
        return;
    }

    const auto output_dir = PathFromQString(output_path_edit_->text());
    std::error_code error;
    if (!std::filesystem::is_directory(output_dir, error) || error) {
        QMessageBox::warning(this, "无效输出目录", "输出目录不存在或不可写。");
        return;
    }

    SetBusy(true);
    status_label_->setText("正在导出 describer 文本…");
    struct ExportState {
        std::filesystem::path output_path;
        std::size_t file_count = 0;
        std::exception_ptr error;
    };
    const auto state = std::make_shared<ExportState>();
    state->file_count = export_file_count_;
    const auto report = export_report_;
    worker_thread_ = QThread::create([state, report, output_dir] {
        try {
            const auto output_path = ReportWriter::ChooseOutputPath(output_dir);
            std::string write_error;
            if (!ReportWriter::WriteUtf8(output_path, *report, write_error)) {
                throw std::runtime_error(write_error);
            }
            state->output_path = output_path;
        } catch (...) {
            state->error = std::current_exception();
        }
    });
    worker_thread_->setParent(this);
    connect(worker_thread_, &QThread::finished, this, [this, state] {
        worker_thread_ = nullptr;
        try {
            if (state->error) std::rethrow_exception(state->error);
            status_label_->setText(
                QString("已导出 %1 个文件：%2")
                    .arg(static_cast<qulonglong>(state->file_count))
                    .arg(PathToQString(state->output_path.filename())));
            status_label_->setToolTip(PathToQString(state->output_path));
            LOG_INFO("Exported {} selected files to {}", state->file_count,
                     PathToUtf8(state->output_path));
        } catch (const std::exception& e) {
            LOG_ERROR("Export failed: {}", e.what());
            status_label_->setText("导出失败，请查看日志。");
            QMessageBox::critical(this, "导出失败", QString::fromUtf8(e.what()));
        }
        SetBusy(false);
    });
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);
    worker_thread_->start();
}

void MainWindow::SetBusy(bool busy) {
    load_btn_->setEnabled(!busy);
    start_btn_->setEnabled(!busy && !loaded_project_path_.isEmpty() && export_report_ != nullptr);
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
