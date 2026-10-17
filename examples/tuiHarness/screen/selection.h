/// @file
/// @brief What the mouse picked out on the screen, and the text it covers.

#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "canvas.h"

namespace OpenAnimaTui::Screen {

/// A span of the screen, from where the button went down to where the mouse is now.
///
/// The client draws the screen, so it is the only one that can say what a region of it holds:
/// the terminal's own selection is out of action while the mouse is reported here, which is the
/// price of the wheel scrolling the conversation.
class Selection {
public:
    /// Starts a selection at a position.
    /// @param row Row where the button went down.
    /// @param column Column where the button went down.
    // A position on the screen is a row and a column, and they are named at every call site.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void begin(std::size_t row, std::size_t column);

    /// Moves the moving end of the selection.
    /// @param row Row the mouse is at.
    /// @param column Column the mouse is at.
    // As above: a row and a column, both named where they are used.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void extend(std::size_t row, std::size_t column);

    /// Forgets the selection.
    void clear();

    /// @return Whether the mouse is holding something picked out, a single cell included.
    [[nodiscard]] bool active() const;

    /// @return Whether there is nothing picked out at all. A selection of one cell is a
    /// selection: picking out a single character is something a reader asks for.
    [[nodiscard]] bool empty() const;

    /// @param row Row to look at.
    /// @param column Column to look at.
    /// @return Whether the cell is inside the selection.
    [[nodiscard]] bool contains(std::size_t row, std::size_t column) const;

    /// @return First row the selection touches.
    [[nodiscard]] std::size_t firstRow() const;

    /// @return Last row the selection touches.
    [[nodiscard]] std::size_t lastRow() const;

private:
    bool m_active{false};           ///< Whether the mouse picked something out.
    std::size_t m_anchorRow{0};     ///< Row where the button went down.
    std::size_t m_anchorColumn{0};  ///< Column where the button went down.
    std::size_t m_cursorRow{0};     ///< Row the mouse is at.
    std::size_t m_cursorColumn{0};  ///< Column the mouse is at.
};

/// Reads what a selection covers off the frame that was painted.
///
/// Rows are joined by line breaks, trailing blanks of each row are dropped, and the right half of
/// a wide character contributes nothing because the character itself already covers the columns.
/// What the client draws for itself, the prompt marker and the scrollbar, is not part of what is
/// copied: a reader copies what is said, not the frame it is said on.
/// @param frame Frame the selection was made on.
/// @param selection Region to read.
/// @return The text, empty when nothing is covered.
[[nodiscard]] std::string selectedText(const Canvas& frame, const Selection& selection);

/// The word under a position, the way a terminal picks one out on a double click.
///
/// Letters, digits and underscores run together, and so does anything that is not ASCII. A cell
/// that is not part of a word is a word of its own, so a double click on a comma takes the comma.
/// @param frame Frame that was painted.
/// @param row Row the pointer is on.
/// @param column Column the pointer is on.
/// @return The selection that covers the word, empty when the position is off the frame.
// As everywhere here: a row and a column, both named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] Selection wordAt(const Canvas& frame, std::size_t row, std::size_t column);

/// The whole row under a position, the way a terminal picks one out on a triple click.
/// @param frame Frame that was painted.
/// @param row Row the pointer is on.
/// @return The selection that covers the row, empty when the position is off the frame.
[[nodiscard]] Selection lineAt(const Canvas& frame, std::size_t row);

/// Counts presses that come fast and in the same cell, which is what a double click is.
class Clicks {
public:
    /// Records a press.
    /// @param row Row pressed.
    /// @param column Column pressed.
    /// @param when When it was pressed.
    /// @return How many presses in a row this one makes: one, two, three or more.
    // As everywhere here: a row and a column, both named where they are used.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    int press(std::size_t row, std::size_t column, std::chrono::steady_clock::time_point when);

    /// @return How many presses the last one made.
    [[nodiscard]] int count() const noexcept {
        return m_count;
    }

private:
    std::chrono::steady_clock::time_point m_at;  ///< When the last press happened.
    std::size_t m_row{0};                        ///< Row of the last press.
    std::size_t m_column{0};                     ///< Column of the last press.
    int m_count{0};                              ///< Presses in the run so far.
};

}  // namespace OpenAnimaTui::Screen
