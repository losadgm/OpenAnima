/// @file
/// @brief The whole screen as rows, from the model and nothing else.

#pragma once

#include <cstddef>
#include <string>

#include "app/model.h"

namespace OpenAnimaTui::Screen {

/// What a full-screen draw produced: the rows and where the caret goes.
struct RenderResult {
    std::string text;         ///< Rows joined by \n, each padded to the full width.
    std::size_t caretRow{0};  ///< Row of the caret, 0-based.
    std::size_t caretCol{0};  ///< Column of the caret, 0-based.
};

/// Renders the whole screen: a viewport of the conversation, the command hints when one is
/// being typed, and the prompt in a panel pinned at the bottom.
/// @param model What to draw.
/// @param width Columns available.
/// @param height Rows available.
/// @return The rows and where the caret sits.
RenderResult render(const App::Model& model, std::size_t width, std::size_t height);

}  // namespace OpenAnimaTui::Screen
