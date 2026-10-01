// SPDX-License-Identifier: GPL-3.0-or-later
// A game's error dialog without a UI. Eden's built-in one writes the error to the log and never
// answers, so a game that shows an error waits on it for good (seen on the console: a game that
// could not go online stopped at its "Saving..." screen). This one writes the error to the log
// and answers at once, as if the player had closed the dialog, and the game carries on.
#pragma once
#include <chrono>
#include <cstdio>
#include <string>
#include "core/frontend/applets/error.h"
#include "diagnostics.h"

namespace Eden {
class LoggedErrorApplet final : public Core::Frontend::ErrorApplet {
public:
    void Close() const override {}

    void ShowError(Result error, FinishedCallback finished) const override {
        Note(error, {});
        finished();
    }

    void ShowErrorWithTimestamp(Result error, std::chrono::seconds, FinishedCallback finished) const override {
        Note(error, {});
        finished();
    }

    void ShowCustomErrorText(Result error, std::string main_text, std::string detail_text,
                             FinishedCallback finished) const override {
        Note(error, main_text.empty() ? detail_text : main_text);
        finished();
    }

private:
    // The code as the console shows it: 2 and the module, then the description.
    static void Note(Result error, const std::string& text) {
        char line[256];
        std::snprintf(line, sizeof(line), "The game showed error 2%03u-%04u%s%.160s; it was closed for the player",
                      static_cast<unsigned>(error.GetModule()), static_cast<unsigned>(error.GetDescription()),
                      text.empty() ? "" : ": ", text.c_str());
        Report("game error", line);
    }
};
}
