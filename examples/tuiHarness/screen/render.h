/// @file
/// @brief What the client shows, decided before a single byte is written.

#pragma once

#include <cstddef>

#include "app/model.h"
#include "canvas.h"
#include "menu.h"
#include "selection.h"

namespace OpenAnimaTui::Screen {

/// Measurements the screen is laid out on.
namespace Metrics {

constexpr std::size_t PrefixWidth = 8;  ///< Columns a speaker prefix takes.
constexpr std::size_t PromptWidth = 2;  ///< Columns the prompt marker and its space take.
constexpr std::size_t BoxWidth = 1;     ///< Columns the frame around the prompt takes per side.
constexpr std::size_t PanelPad = 1;     ///< Columns of panel kept empty inside the frame, a side.
constexpr std::size_t MaxHints = 6;     ///< Command hints shown at once.
constexpr std::size_t HintIndent = 2;   ///< Columns a hint is indented by.

}  // namespace Metrics

/// One frame, ready to be written.
struct RenderResult {
    Canvas frame;                ///< The cells of the screen.
    std::size_t caretRow{0};     ///< Row the caret belongs in, when a prompt is being typed.
    std::size_t caretColumn{0};  ///< Column the caret belongs in.
    /// Rows hidden above the viewport when it is scrolled all the way up, which is the
    /// largest offset the model can hold.
    std::size_t maxScrollOffset{0};
};

/// Paints a model into a canvas of a given size.
///
/// Every row is decided here and nowhere else: the conversation, the hints that float above the
/// prompt, the status line and the prompt itself, in that order from the top. Nothing is
/// written to a terminal, so the whole screen can be compared with the frame before it.
/// @param model Model to show.
/// @param width Columns available.
/// @param height Rows available.
/// @param selection What the mouse picked out, painted over everything else.
/// @param menu The menu the right button opened, painted over the selection.
/// @return The frame and where the caret goes.
[[nodiscard]] RenderResult render(const App::Model& model, std::size_t width, std::size_t height,
                                  const Selection& selection = Selection{},
                                  const Menu& menu = Menu{});

}  // namespace OpenAnimaTui::Screen
