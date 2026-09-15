#include "gui/main_window.hpp"

#include <sstream>

#include <QtCore/QDir>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QVBoxLayout>

#include "application/project_describer.hpp"
#include "utils/logging.hpp"
#include "utils/user_notifier.hpp"

namespace project_describer::gui {
namespace {

constexpr int kDefaultColumnWidth = 400;

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent) {
    SetupUI();

    project_path_edit_->setText(QDir::currentPath());
    output_path_edit_->setText(QDir::currentPath());
    status_label_->setText("Ready. Pick a project folder to describe.");

    LOG_INFO("GUI window created");
}

MainWindow::~MainWindow() {
    LOG_INFO("GUI window destroyed");
}

void MainWindow::SetupUI() {
    setWindowTitle("Project Describer");
    resize(900, 640);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    // Folder row 1: project root
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("Project folder:", central));
        project_path_edit_ = new QLineEdit(central);
        row->addWidget(project_path_edit_, /*stretch=*/1);
        browse_project_btn_ = new QPushButton("Browse…", central);
        connect(browse_project_btn_, &QPushButton::clicked, this, &MainWindow::OnBrowseProject);
        row->addWidget(browse_project_btn_);
        layout->addLayout(row);
    }
    // Folder row 2: output directory
    {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel("Output folder:", central));
        output_path_edit_ = new QLineEdit(central);
        row->addWidget(output_path_edit_, /*stretch=*/1);
        browse_output_btn_ = new QPushButton("Browse…", central);
        connect(browse_output_btn_, &QPushButton::clicked, this, &MainWindow::OnBrowseOutput);
        row->addWidget(browse_output_btn_);
        layout->addLayout(row);
    }
    // Start button
    {
        start_btn_ = new QPushButton("Start", central);
        connect(start_btn_, &QPushButton::clicked, this, &MainWindow::OnStart);
        layout->addWidget(start_btn_);
    }
    // Tree view showing scanned description
    {
        tree_view_ = new QTreeView(central);
        tree_model_ = new QStandardItemModel(this);
        tree_model_->setHorizontalHeaderLabels({"Path", "Lines", "Encoding"});
        tree_view_->setModel(tree_model_);
        tree_view_->setColumnWidth(0, kDefaultColumnWidth);
        tree_view_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        layout->addWidget(tree_view_, /*stretch=*/1);
    }
    // Preview
    {
        preview_edit_ = new QTextEdit(central);
        preview_edit_->setReadOnly(true);
        preview_edit_->setPlaceholderText(
            "Report preview will appear here. Full text is written to describer.txt.");
        layout->addWidget(preview_edit_, /*stretch=*/1);
    }
    // Status bar area
    {
        auto* status_layout = new QHBoxLayout;
        status_label_ = new QLabel(central);
        status_layout->addWidget(status_label_, /*stretch=*/1);
        progress_bar_ = new QProgressBar(central);
        progress_bar_->setRange(0, 0);          // indeterminate
        progress_bar_->setFixedWidth(160);
        progress_bar_->hide();
        status_layout->addWidget(progress_bar_);
        layout->addLayout(status_layout);
    }

    setCentralWidget(central);
}

void MainWindow::OnBrowseProject() {
    try {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "Select project folder", project_path_edit_->text());
        if (!dir.isEmpty()) {
            project_path_edit_->setText(QDir::toNativeSeparators(dir));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error in folder dialog: {}", e.what());
        QMessageBox::critical(this, "Error",
                              "Failed to open folder dialog:\n" +
                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::OnBrowseOutput() {
    try {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "Select output folder", output_path_edit_->text());
        if (!dir.isEmpty()) {
            output_path_edit_->setText(QDir::toNativeSeparators(dir));
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error in folder dialog: {}", e.what());
        QMessageBox::critical(this, "Error",
                              "Failed to open folder dialog:\n" +
                              QString::fromUtf8(e.what()));
    }
}

void MainWindow::OnStart() {
    LOG_INFO("Start clicked; project='{}' output='{}'",
             project_path_edit_->text().toStdString(),
             output_path_edit_->text().toStdString());

    if (project_path_edit_->text().isEmpty()) {
        QMessageBox::warning(this, "Missing project folder",
                             "Please select a project folder first.");
        return;
    }

    SetBusy(true);
    status_label_->setText("Scanning…");
    QApplication::processEvents();   // flush UI before blocking scan

    try {
        project_describer::CommandLineOptions options;
        options.project_root = std::filesystem::path(
            project_path_edit_->text().toStdString());
        options.output_directory = std::filesystem::path(
            output_path_edit_->text().toStdString());

        // Capture stdout/stderr output into strings (GUI doesn't use terminal)
        std::ostringstream captured_output;
        std::ostringstream captured_errors;

        const int rc = project_describer::RunProjectDescriber(
            options, captured_output, captured_errors);   // 3rd arg is std::ostream&

        preview_edit_->setPlainText(
            QString::fromStdString(captured_output.str()) + "\n" +
            (captured_errors.str().empty()
                 ? QString()
                 : QString("[stderr]\n") +
                       QString::fromStdString(captured_errors.str())));

        if (rc != 0) {
            status_label_->setText("Failed — see log for details.");
            const auto err_text = captured_errors.str();
            QMessageBox::critical(
                this, "Run failed",
                "project-describer returned exit code " +
                    QString::number(rc) + ".\n" +
                (err_text.empty()
                     ? QString("See describer.log for details.")
                     : QString::fromStdString(err_text)));
            LOG_ERROR("Run failed, exit code {}", rc);
        } else {
            status_label_->setText("Done!");
        }
    } catch (const std::bad_alloc& e) {
        LOG_CRITICAL("Out of memory: {}", e.what());
        status_label_->setText("Out of memory.");
        QMessageBox::critical(this, "Out of memory",
                              "Not enough memory to complete the operation:\n" +
                              QString::fromUtf8(e.what()));
    } catch (const std::filesystem::filesystem_error& e) {
        LOG_CRITICAL("Filesystem error: {}", e.what());
        status_label_->setText("Filesystem error — see log.");
        QMessageBox::critical(this, "Filesystem error",
                              QString::fromUtf8(e.what()));
    } catch (const std::exception& e) {
        LOG_CRITICAL("Unhandled exception in OnStart: {}", e.what());
        status_label_->setText("Unexpected error — see log.");
        QMessageBox::critical(this, "Unexpected error",
                              "An unexpected error occurred:\n" +
                              QString::fromUtf8(e.what()));
    }

    SetBusy(false);
}

void MainWindow::SetBusy(bool busy) {
    start_btn_->setEnabled(!busy);
    browse_project_btn_->setEnabled(!busy);
    browse_output_btn_->setEnabled(!busy);
    if (busy) {
        progress_bar_->setRange(0, 0);   // indeterminate
        progress_bar_->show();
    } else {
        progress_bar_->hide();
    }
}

}  // namespace project_describer::gui
