#include <QtWidgets/QApplication>
#include <QtWidgets/QMessageBox>

#include "gui/main_window.hpp"
#include "utils/logging.hpp"
#include "utils/user_notifier.hpp"

namespace {

void GuiErrorMessage(const std::string& title, const std::string& detail) {
    // We don't have a widget pointer here yet — show via global helper
    QMessageBox::critical(nullptr,
                          QString::fromUtf8(title.c_str()),
                          QString::fromUtf8(detail.c_str()));
}

void GuiInfoMessage(const std::string& message) {
    // Intentionally silent — GUI shows info in the status bar, not a popup
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        QApplication app(argc, argv);
        QApplication::setApplicationName("Project Describer");
        QApplication::setApplicationVersion("0.1.0");
        QApplication::setOrganizationName("ProjectDescriber");

        // Initialize logging — file goes next to the executable
        project_describer::log::Initialize(
            project_describer::log::Mode::kGui,
            QApplication::applicationDirPath().toStdString());

        project_describer::SetUserErrorCallback(&GuiErrorMessage);
        project_describer::SetUserInfoCallback(&GuiInfoMessage);

        project_describer::gui::MainWindow window;
        window.show();

        return app.exec();
    } catch (const std::exception& e) {
        LOG_CRITICAL("Fatal error before GUI startup: {}", e.what());
        QMessageBox::critical(nullptr, "Fatal Error",
                              QString::fromUtf8(e.what()));
        return 1;
    }
}
