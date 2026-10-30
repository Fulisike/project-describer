#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <functional>

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QSettings>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtWidgets/QApplication>
#include <QtWidgets/QSplitter>

#include "gui/main_window.hpp"
#include "utils/logging.hpp"

namespace {

void Write(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << content;
}

QPushButton* Button(QWidget& window, const QString& label) {
    for (auto* button : window.findChildren<QPushButton*>()) {
        if (button->text() == label) {
            return button;
        }
    }
    throw std::runtime_error("Missing GUI button");
}

QStandardItem* Find(QStandardItem* parent, const QString& label) {
    for (int row = 0; row < parent->rowCount(); ++row) {
        auto* child = parent->child(row);
        if (child->text() == label) {
            return child;
        }
        if (auto* found = Find(child, label)) {
            return found;
        }
    }
    return nullptr;
}

void WaitFor(const std::function<bool()>& ready) {
    QElapsedTimer deadline;
    deadline.start();
    while (!ready() && deadline.elapsed() < 10000) {
        QApplication::processEvents();
        QThread::msleep(1);
    }
    EXPECT_TRUE(ready());
}

}  // namespace

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    project_describer::log::Initialize(project_describer::log::Mode::kCli,
                                       std::filesystem::temp_directory_path());
    QTemporaryDir temporary;
    EXPECT_TRUE(temporary.isValid());
    QCoreApplication::setOrganizationName("ProjectDescriber");
    QCoreApplication::setApplicationName("Project Describer");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());

    const auto original_cwd = QDir::currentPath();
    EXPECT_TRUE(QDir::setCurrent(temporary.path()));
    const auto executable_dir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    const auto remembered_project = QDir::toNativeSeparators(temporary.path() + "/项目 目录");
    const auto remembered_output = QDir::toNativeSeparators(temporary.path() + "/输出 目录");
    EXPECT_TRUE(QDir().mkpath(remembered_project));
    EXPECT_TRUE(QDir().mkpath(remembered_output));
    {
        project_describer::gui::MainWindow window;
        const auto fields = window.findChildren<QLineEdit*>();
        EXPECT_EQ(fields[0]->text(), executable_dir);
        EXPECT_EQ(fields[1]->text(), executable_dir);
        auto* splitter = window.findChild<QSplitter*>("preview_splitter");
        EXPECT_TRUE(splitter != nullptr);
        if (splitter) {
            EXPECT_EQ(splitter->count(), 2);
            EXPECT_EQ(splitter->orientation(), Qt::Horizontal);
        }
        auto* file_preview = window.findChild<QPlainTextEdit*>("file_preview");
        auto* export_preview = window.findChild<QPlainTextEdit*>("export_preview");
        EXPECT_TRUE(file_preview->toPlainText().isEmpty());
        EXPECT_TRUE(file_preview->placeholderText().contains("选择一个文件"));
        EXPECT_TRUE(export_preview->toPlainText().isEmpty());
        fields[0]->setText("./项目 目录");
        fields[1]->setText(remembered_output);
    }
    EXPECT_TRUE(QDir::setCurrent(original_cwd));
    {
        project_describer::gui::MainWindow window;
        const auto fields = window.findChildren<QLineEdit*>();
        EXPECT_EQ(fields[0]->text(), remembered_project);
        EXPECT_EQ(fields[1]->text(), remembered_output);
        fields[0]->clear();
        fields[1]->setText("   ");
    }
    {
        project_describer::gui::MainWindow window;
        const auto fields = window.findChildren<QLineEdit*>();
        EXPECT_EQ(fields[0]->text(), remembered_project);
        EXPECT_EQ(fields[1]->text(), remembered_output);
    }
    QSettings().remove("paths/output");
    {
        project_describer::gui::MainWindow window;
        const auto fields = window.findChildren<QLineEdit*>();
        EXPECT_EQ(fields[0]->text(), remembered_project);
        EXPECT_EQ(fields[1]->text(), executable_dir);
        fields[1]->setText(remembered_output);
    }
    QSettings().remove("paths/project");
    {
        project_describer::gui::MainWindow window;
        const auto fields = window.findChildren<QLineEdit*>();
        EXPECT_EQ(fields[0]->text(), executable_dir);
        EXPECT_EQ(fields[1]->text(), remembered_output);
    }
    QSettings().clear();
    const std::filesystem::path root(temporary.path().toStdWString());
    const auto project = root / "project";
    std::filesystem::create_directories(project / ".git");
    const auto codex = root / "codex";
    const auto claude = root / "claude";
    qputenv("CODEX_HOME", QByteArray::fromStdString(codex.string()));
    qputenv("CLAUDE_CONFIG_DIR", QByteArray::fromStdString(claude.string()));
    const auto cwd = project.generic_string();
    Write(project / "AGENTS.md", "shared Codex instructions");
    Write(project / "CLAUDE.md", "shared Claude instructions");
    Write(codex / "sessions" / "session.jsonl",
        "{\"type\":\"session_meta\",\"payload\":{\"id\":\"codex-session\",\"cwd\":\"" + cwd + "\"}}\n" +
        R"({"type":"response_item","payload":{"type":"message","role":"user","content":[{"type":"input_text","text":"\u4f60\u597d GUI context"}]}})" "\n");
    Write(claude / "projects" / "custom" / "session.jsonl",
        "{\"type\":\"user\",\"sessionId\":\"claude-session\",\"cwd\":\"" +
        (root / "original-project").generic_string() +
        R"(","message":{"role":"user","content":"excluded Claude transcript"}})" "\n");

    {
        project_describer::gui::MainWindow window;
        const auto fields = window.findChildren<QLineEdit*>();
        fields[0]->setText(QString::fromStdWString(project.wstring()));
        fields[1]->setText(QString::fromStdWString(project.wstring()));
        for (auto* checkbox : window.findChildren<QCheckBox*>()) {
            checkbox->setChecked(checkbox->text() != "项目文件");
        }
        auto* load = Button(window, "加载 / 刷新");
        int ticks = 0;
        QTimer heartbeat;
        QObject::connect(&heartbeat, &QTimer::timeout, [&ticks] { ++ticks; });
        heartbeat.start(0);
        load->click();
        EXPECT_TRUE(!load->isEnabled());
        EXPECT_TRUE(!fields[0]->isEnabled());
        QElapsedTimer deadline;
        deadline.start();
        while (!load->isEnabled() && deadline.elapsed() < 10000) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        heartbeat.stop();
        EXPECT_TRUE(load->isEnabled());
        EXPECT_TRUE(ticks > 0);
        WaitFor([&window] { return Button(window, "导出所选文件")->isEnabled(); });
        auto* file_preview = window.findChild<QPlainTextEdit*>("file_preview");
        auto* export_preview = window.findChild<QPlainTextEdit*>("export_preview");
        EXPECT_TRUE(file_preview->toPlainText().isEmpty());
        EXPECT_TRUE(export_preview->toPlainText().contains("GUI context"));
        EXPECT_TRUE(export_preview->toPlainText().contains("excluded Claude transcript"));
        auto* tree = window.findChild<QTreeView*>();
        auto* model = qobject_cast<QStandardItemModel*>(tree->model());
        auto* codex_session = Find(model->invisibleRootItem(), "codex-session.md");
        auto* claude_session = Find(model->invisibleRootItem(), "claude-session.md");
        EXPECT_TRUE(codex_session != nullptr);
        EXPECT_TRUE(claude_session != nullptr);
        EXPECT_TRUE(Find(model->invisibleRootItem(), "Shared context") != nullptr);
        if (codex_session && claude_session) {
            tree->setCurrentIndex(codex_session->index());
            EXPECT_TRUE(file_preview->toPlainText().contains("你好 GUI context"));
            tree->setCurrentIndex(codex_session->parent()->index());
            EXPECT_TRUE(file_preview->toPlainText().isEmpty());
            tree->setCurrentIndex(codex_session->index());
            tree->selectionModel()->clearSelection();
            EXPECT_TRUE(file_preview->toPlainText().isEmpty());
            tree->setCurrentIndex(codex_session->index());
            codex_session->parent()->setCheckState(Qt::Unchecked);
            EXPECT_EQ(codex_session->checkState(), Qt::Unchecked);
            codex_session->parent()->setCheckState(Qt::Checked);
            EXPECT_EQ(codex_session->checkState(), Qt::Checked);
            claude_session->setCheckState(Qt::Unchecked);
            WaitFor([&window] { return Button(window, "导出所选文件")->isEnabled(); });
            EXPECT_TRUE(!export_preview->toPlainText().contains("excluded Claude transcript"));
            const auto expected_export = export_preview->toPlainText().toUtf8().toStdString();
            Button(window, "导出所选文件")->click();
            EXPECT_TRUE(!load->isEnabled());
            deadline.restart();
            while (!load->isEnabled() && deadline.elapsed() < 10000) {
                QApplication::processEvents();
                QThread::msleep(1);
            }
            EXPECT_TRUE(load->isEnabled());
            std::ifstream report(project / "describer.txt", std::ios::binary);
            const std::string content((std::istreambuf_iterator<char>(report)), {});
            EXPECT_TRUE(content.find("GUI context") != std::string::npos);
            EXPECT_TRUE(content.find("excluded Claude transcript") == std::string::npos);
            EXPECT_EQ(content, expected_export);
            EXPECT_EQ(export_preview->toPlainText().toUtf8().toStdString(), expected_export);
            fields[0]->setText(fields[0]->text() + "/.");
            EXPECT_TRUE(!Button(window, "导出所选文件")->isEnabled());
            EXPECT_TRUE(file_preview->toPlainText().isEmpty());
            EXPECT_TRUE(export_preview->toPlainText().isEmpty());
        }
    }
    {
        Write(project / "Codex" / "source.txt", "project file named like a context root");
        Write(project / "line-limit.txt", "first\nsecond\nthird\n");
        project_describer::gui::MainWindow window;
        const auto fields = window.findChildren<QLineEdit*>();
        fields[0]->setText(QString::fromStdWString(project.wstring()));
        fields[1]->setText(QString::fromStdWString(project.wstring()));
        auto* load = Button(window, "加载 / 刷新");
        load->click();
        QElapsedTimer deadline;
        deadline.start();
        while (!load->isEnabled() && deadline.elapsed() < 10000) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        auto* tree = window.findChild<QTreeView*>();
        auto* model = qobject_cast<QStandardItemModel*>(tree->model());
        auto* file_preview = window.findChild<QPlainTextEdit*>("file_preview");
        auto* export_preview = window.findChild<QPlainTextEdit*>("export_preview");
        auto* source = Find(model->invisibleRootItem(), "source.txt");
        EXPECT_TRUE(source != nullptr);
        if (source) {
            EXPECT_TRUE(source->parent() && source->parent()->parent());
            if (source->parent() && source->parent()->parent()) {
                EXPECT_EQ(source->parent()->parent()->text(), QString("项目文件"));
            }
        }
        Button(window, "全不选")->click();
        WaitFor([export_preview] {
            return export_preview->placeholderText().contains("勾选需要导出") &&
                   export_preview->toPlainText().isEmpty();
        });
        EXPECT_TRUE(!Button(window, "导出所选文件")->isEnabled());
        auto* limited = Find(model->invisibleRootItem(), "line-limit.txt");
        EXPECT_TRUE(limited != nullptr);
        if (limited) {
            limited->setCheckState(Qt::Checked);
            tree->setCurrentIndex(limited->index());
        }
        window.findChild<QLineEdit*>("file_line_limit")->setText("2");
        WaitFor([&window] { return Button(window, "导出所选文件")->isEnabled(); });
        EXPECT_TRUE(export_preview->toPlainText().contains("first\nsecond\n"));
        EXPECT_TRUE(!export_preview->toPlainText().contains("third"));
        EXPECT_TRUE(file_preview->toPlainText().contains("third"));
        const auto expected_export = export_preview->toPlainText().toUtf8().toStdString();
        Button(window, "导出所选文件")->click();
        deadline.restart();
        while (!load->isEnabled() && deadline.elapsed() < 10000) {
            QApplication::processEvents();
            QThread::msleep(1);
        }
        std::ifstream report(project / "describer1.txt", std::ios::binary);
        const std::string content((std::istreambuf_iterator<char>(report)), {});
        EXPECT_TRUE(content.find("first\nsecond\n") != std::string::npos);
        EXPECT_TRUE(content.find("third") == std::string::npos);
        EXPECT_EQ(content, expected_export);
        Button(window, "默认（全文）")->click();
        WaitFor([&window] { return Button(window, "导出所选文件")->isEnabled(); });
        EXPECT_TRUE(export_preview->toPlainText().contains("third"));
        window.findChild<QLineEdit*>("file_line_limit")->setText("0");
        WaitFor([export_preview] { return export_preview->placeholderText().contains("请输入正整数"); });
        EXPECT_TRUE(!Button(window, "导出所选文件")->isEnabled());
        for (auto* checkbox : window.findChildren<QCheckBox*>()) {
            if (checkbox->text() == "Codex 上下文") checkbox->setChecked(true);
        }
        EXPECT_TRUE(!Button(window, "导出所选文件")->isEnabled());
    }
    project_describer::log::Shutdown();
    return test::Failures() == 0 ? 0 : 1;
}
