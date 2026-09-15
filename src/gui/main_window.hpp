#pragma once

#include <memory>

#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtGui/QStandardItemModel>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QTreeView>

namespace project_describer::gui {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void OnBrowseProject();
    void OnBrowseOutput();
    void OnStart();

private:
    void SetupUI();
    void SetBusy(bool busy);

    QLineEdit*       project_path_edit_   = nullptr;
    QLineEdit*       output_path_edit_    = nullptr;
    QPushButton*     browse_project_btn_  = nullptr;
    QPushButton*     browse_output_btn_   = nullptr;
    QPushButton*     start_btn_           = nullptr;
    QTreeView*       tree_view_           = nullptr;
    QStandardItemModel* tree_model_       = nullptr;
    QTextEdit*       preview_edit_        = nullptr;
    QLabel*          status_label_        = nullptr;
    QProgressBar*    progress_bar_        = nullptr;
};

}  // namespace project_describer::gui
