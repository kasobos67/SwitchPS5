// SPDX-License-Identifier: GPL-3.0-or-later
// The app version, as the launcher and the crash report show it. Its one home is
// sce_sys/param.json (contentVersion): the build writes app_version.h from that file
// (headless/CMakeLists.txt), and the package carries the same file.
#pragma once

#include "app_version.h"

namespace Eden {
inline constexpr const char* kAppVersion = EDEN_APP_VERSION;
}
