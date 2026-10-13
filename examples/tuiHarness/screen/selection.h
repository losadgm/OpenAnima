/// @file
/// @brief What the mouse picked out on the screen, and the text it covers.

#pragma once

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

    /// @return Whether there is nothing worth taking: a selection the mouse never left, which is
    /// what a click leaves behind, and one that was never started.
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
/// @param frame Frame the selection was made on.
/// @param selection Region to read.
/// @return The text, empty when nothing is covered.
[[nodiscard]] std::string selectedText(const Canvas& frame, const Selection& selection);

}  // namespace OpenAnimaTui::Screen
