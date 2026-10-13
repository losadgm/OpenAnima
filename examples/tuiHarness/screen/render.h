/// @file
/// @brief What the client shows, decided before a single byte is written.

#pragma once

#include <cstddef>

#include "app/model.h"
#include "canvas.h"

namespace OpenAnimaTui::Screen {

/// Measurements the screen is laid out on.
namespace Metrics {

constexpr std::size_t PrefixWidth = 8;  ///< Columns a speaker prefix takes.
constexpr std::size_t PromptWidth = 2;  ///< Columns the prompt marker and its space take.
constexpr std::size_t MaxHints = 6;     ///< Command hints shown at once.
constexpr std::size_t HintIndent = 2;   ///< Columns a hint is indented by.

}  // namespace Metrics

/// One frame, ready to be written.
struct RenderResult {
    Canvas frame;                ///< The cells of the screen.
    std::size_t caretRow{0};     ///< Row the caret belongs in, when a prompt is being typed.
    std::size_t caretColumn{0};  ///< Column the caret belongs in.
};

/// Paints a model into a canvas of a given size.
///
/// Every row is decided here and nowhere else: the conversation, the hints that float above the
/// prompt, the status line and the prompt itself, in that order from the top. Nothing is
/// written to a terminal, so the whole screen can be compared with the frame before it.
/// @param model Model to show.
/// @param width Columns available.
/// @param height Rows available.
/// @return The frame and where the caret goes.
[[nodiscard]] RenderResult render(const App::Model& model, std::size_t width, std::size_t height);

}  // namespace OpenAnimaTui::Screen
