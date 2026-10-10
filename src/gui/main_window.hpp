#pragma once

#include <memory>
#include <QtCore/QThread>

#include <QtWidgets/QCheckBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTextEdit>
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
    QTextEdit*       preview_edit_        = nullptr;
    QLabel*          status_label_        = nullptr;
    QProgressBar*    progress_bar_        = nullptr;
    ScanResult       loaded_scan_;
    QString          loaded_project_path_;
    bool             updating_checks_     = false;
    QThread*         load_thread_         = nullptr;
};

}  // namespace project_describer::gui
