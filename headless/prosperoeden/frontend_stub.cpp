// SPDX-License-Identifier: GPL-3.0-or-later
// Builds without the PS5 OpenGL SDK have no launcher: it draws with OpenGL.
#include "frontend.h"

#include "diagnostics.h"

std::string SelectProsperoEdenGame(const std::string&) {
    Eden::Report("menu", "This build has no launcher (it needs the PS5 OpenGL SDK)");
    return {};
}
