#pragma once

#include <functional>
#include <string>

namespace project_describer {

/// Each frontend (CLI / GUI) registers the appropriate overload at startup.
/// The core library then uses these to surface actionable errors to the human
/// without depending on either iostream or Qt directly.

using UserErrorCallback = void(const std::string& title, const std::string& detail);
using UserInfoCallback  = void(const std::string& message);

void SetUserErrorCallback(UserErrorCallback* cb);
void SetUserInfoCallback(UserInfoCallback* cb);

/// Log at ERROR / INFO level AND surface to the user via the registered callback.
void NotifyUserError(const std::string& title, const std::string& detail);
void NotifyUserInfo(const std::string& message);

}  // namespace project_describer
