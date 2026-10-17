/// @file
/// @brief What the client shows, decided before a single byte is written.

#pragma once

#include <cstddef>

#include "canvas.h"
#include "domain/model.h"
#include "selection.h"

namespace OpenAnimaTui::Presentation {

/// Measurements the screen is laid out on.
namespace Metrics {

constexpr std::size_t PrefixWidth = 8;  ///< Columns a speaker prefix takes.
constexpr std::size_t PromptWidth = 2;  ///< Columns the prompt marker and its space take.
constexpr std::size_t BoxWidth = 1;     ///< Columns the frame around the prompt takes per side.
constexpr std::size_t PanelPad = 1;     ///< Columns of panel kept empty inside the frame, a side.
constexpr std::size_t MaxHints = 6;     ///< Command hints shown at once.
constexpr std::size_t HintIndent = 2;   ///< Columns a hint is indented by.
/// Columns kept empty after the longest hint, one of which the bar takes when there is one.
constexpr std::size_t HintPad = 3;

}  // namespace Metrics

/// Where the pointer is on the screen, which is what the client lights up under it.
struct Pointer {
    bool onScreen{false};   ///< Whether the pointer is over the screen at all.
    std::size_t row{0};     ///< Row the pointer is over.
    std::size_t column{0};  ///< Column the pointer is over.
};

/// One frame, ready to be written.
struct RenderResult {
    Canvas frame;                   ///< The cells of the screen.
    std::size_t caretRow{0};        ///< Row the caret belongs in, when a prompt is being typed.
    std::size_t caretColumn{0};     ///< Column the caret belongs in.
    std::size_t scrollRows{0};      ///< Rows the conversation viewport has, which is the bar.
    std::size_t transcriptRows{0};  ///< Rows the whole conversation takes, on screen or not.
    std::size_t hintRow{0};         ///< First row of the panel the hints float on.
    std::size_t hintCount{0};       ///< Hints the panel shows, zero when there are none.
    std::size_t hintOffset{0};      ///< Index in the whole list of the first hint shown.
    std::size_t hintColumns{0};     ///< Columns the panel takes, from the left edge.
    std::size_t hintTotal{0};       ///< Hints that match, whether the panel shows them or not.
    /// Rows hidden above the viewport when it is scrolled all the way up, which is the
    /// largest offset the model can hold.
    std::size_t maxScrollOffset{0};
    std::size_t promptFirst{0};  ///< Row of the draft the panel of the prompt starts on.
    std::size_t promptRows{0};   ///< Rows of the draft the panel shows.
    std::size_t promptTotal{0};  ///< Rows the whole draft takes.
};

/// @param transcriptRows Rows the whole conversation takes.
/// @param viewRows Rows the conversation viewport has.
/// @param start First row of the conversation that is on screen.
/// @return The row of the viewport the thumb of the scrollbar begins at.
[[nodiscard]] std::size_t thumbRow(std::size_t transcriptRows, std::size_t viewRows,
                                   std::size_t start);

/// @param transcriptRows Rows the whole conversation takes.
/// @param viewRows Rows the conversation viewport has.
/// @param row Row of the viewport the thumb is dragged to.
/// @return The first row of the conversation that puts the thumb there.
[[nodiscard]] std::size_t firstRowForThumb(std::size_t transcriptRows, std::size_t viewRows,
                                           std::size_t row);

/// @param total Hints that match.
/// @param rows Rows the panel of hints has.
/// @param selected Which hint is selected, counted in the whole list.
/// @return The row of the panel the thumb of its bar begins at.
[[nodiscard]] std::size_t hintThumbRow(std::size_t total, std::size_t rows, std::size_t selected);

/// The hint a row of the panel stands for, which is where dragging the thumb takes it.
/// @param total Hints that match.
/// @param rows Rows the panel of hints has.
/// @param row Row of the panel the thumb is dragged to.
/// @return The hint that row is over, counted in the whole list.
[[nodiscard]] std::size_t hintAtThumbRow(std::size_t total, std::size_t rows, std::size_t row);

/// Paints a model into a canvas of a given size.
///
/// Every row is decided here and nowhere else: the conversation, the hints that float above the
/// prompt, the status line and the prompt itself, in that order from the top. Nothing is
/// written to a terminal, so the whole screen can be compared with the frame before it.
/// @param model Model to show.
/// @param width Columns available.
/// @param height Rows available.
/// @param selection What the mouse picked out, painted over everything else.
/// @param pointer Where the pointer is, which decides what is lit up under it.
/// @return The frame and where the caret goes.
[[nodiscard]] RenderResult render(const Domain::Model& model, std::size_t width, std::size_t height,
                                  const Selection& selection = Selection{},
                                  const Pointer& pointer = Pointer{});

}  // namespace OpenAnimaTui::Presentation
