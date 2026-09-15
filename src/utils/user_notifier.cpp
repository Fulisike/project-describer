#include "utils/user_notifier.hpp"

#include "utils/logging.hpp"

namespace project_describer {
namespace {

UserErrorCallback* g_error_cb = nullptr;
UserInfoCallback*  g_info_cb  = nullptr;

}  // namespace

void SetUserErrorCallback(UserErrorCallback* const cb) { g_error_cb = cb; }
void SetUserInfoCallback(UserInfoCallback* const cb)  { g_info_cb  = cb; }

void NotifyUserError(const std::string& title, const std::string& detail) {
    LOG_ERROR("{}: {}", title, detail);
    if (g_error_cb) {
        g_error_cb(title, detail);
    }
}

void NotifyUserInfo(const std::string& message) {
    LOG_INFO("{}", message);
    if (g_info_cb) {
        g_info_cb(message);
    }
}

}  // namespace project_describer
