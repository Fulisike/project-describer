#pragma once

#include <memory>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QTreeView>

#include "core/scanner.hpp"

namespace project_describer::gui {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OnBrowseProject();
    void OnBrowseOutput();
    void OnLoad();
    void OnStart();
    void OnTreeItemChanged(QStandardItem* item);

private:
    void SetupUI();
    void SetBusy(bool busy);
    void PopulateTree();
    void SetAllChecked(Qt::CheckState state);
    void UpdateFilePreview();
    void ScheduleExportPreview();
    void UpdateExportPreview();
    ScanResult SelectedScan() const;

    QLineEdit*       project_path_edit_   = nullptr;
    QLineEdit*       output_path_edit_    = nullptr;
    QLineEdit*       file_line_limit_edit_ = nullptr;
    QPushButton*     reset_line_limit_btn_ = nullptr;
    QPushButton*     browse_project_btn_  = nullptr;
    QPushButton*     browse_output_btn_   = nullptr;
    QPushButton*     load_btn_            = nullptr;
    QPushButton*     start_btn_           = nullptr;
    QPushButton*     select_all_btn_      = nullptr;
    QPushButton*     select_none_btn_     = nullptr;
    QCheckBox*       project_files_check_ = nullptr;
    QCheckBox*       codex_context_check_ = nullptr;
    QCheckBox*       claude_context_check_= nullptr;
    QTreeView*       tree_view_           = nullptr;
    QStandardItemModel* tree_model_       = nullptr;
    QPlainTextEdit*  file_preview_edit_   = nullptr;
    QPlainTextEdit*  export_preview_edit_ = nullptr;
    QTimer*         preview_timer_       = nullptr;
    QLabel*          status_label_        = nullptr;
    QProgressBar*    progress_bar_        = nullptr;
    ScanResult       loaded_scan_;
    std::size_t      loaded_project_file_count_ = 0;
    QString          loaded_project_path_;
    bool             updating_checks_     = false;
    QThread*         worker_thread_         = nullptr;
    QThread*         preview_thread_        = nullptr;
    std::size_t      preview_revision_      = 0;
    std::shared_ptr<const std::string> export_report_;
    std::size_t      export_file_count_     = 0;
};

}  // namespace project_describer::gui
