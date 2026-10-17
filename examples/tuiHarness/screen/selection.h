/// @file
/// @brief What the mouse picked out on the screen, and the text it covers.

#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include "canvas.h"

namespace OpenAnimaTui::Screen {

/// Where a selection lives.
///
/// The conversation scrolls and the prompt does not, so a span of one is not a span of the
/// other: a selection made in the conversation moves with the text and is read inside the
/// viewport, and one made on the prompt stays where it is and is read on the whole screen.
enum class Scope : std::uint8_t {
    Conversation,  ///< Inside the viewport of the conversation, which scrolls.
    Screen         ///< The prompt and what surrounds it, which stays where it is.
};

/// The rows of the screen a scope takes, which is as far as a selection of it reaches.
///
/// The two scopes do not share a row: the conversation sits at the top of the screen and the
/// prompt and the rows around it take the rest, so a pick of one never touches the other.
struct Bounds {
    std::size_t first{0};  ///< First row of the scope.
    std::size_t count{0};  ///< Rows it takes, which may be none on a screen with no room.

    /// @return Row after the last one of the scope.
    [[nodiscard]] std::size_t end() const;

    /// @return Whether the scope has no row at all.
    [[nodiscard]] bool empty() const;
};

/// @param scope Where a selection lives.
/// @param scrollRows Rows the conversation takes at the top of the screen.
/// @param height Rows the screen has.
/// @return The rows of the screen the scope takes.
[[nodiscard]] Bounds boundsOf(Scope scope, std::size_t scrollRows, std::size_t height);

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
    /// @param scope Where the row is, which decides what the selection does when text scrolls.
    // A position on the screen is a row and a column, and they are named at every call site.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void begin(std::size_t row, std::size_t column, Scope scope = Scope::Conversation);

    /// Moves the moving end of the selection.
    /// @param row Row the mouse is at.
    /// @param column Column the mouse is at.
    // As above: a row and a column, both named where they are used.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void extend(std::size_t row, std::size_t column);

    /// Forgets the selection.
    void clear();

    /// Moves the selection with the text as the conversation scrolls.
    ///
    /// Only a selection made in the conversation moves: the prompt does not scroll. The span
    /// keeps the rows it was made on, which may be outside the viewport for a while, so it is
    /// there again when the text comes back. A selection of the rest of the screen is left
    /// alone.
    /// @param rows Rows the text moved down, negative when it moved up.
    void shift(std::ptrdiff_t rows);

    /// @return Whether the mouse is holding something picked out, a single cell included.
    [[nodiscard]] bool active() const;

    /// @return Whether there is nothing picked out at all. A selection of one cell is a
    /// selection: picking out a single character is something a reader asks for.
    [[nodiscard]] bool empty() const;

    /// @param row Row to look at.
    /// @param column Column to look at.
    /// @return Whether the cell is inside the selection.
    [[nodiscard]] bool contains(std::size_t row, std::size_t column) const;

    /// @return Where the selection lives, which is what it moves with.
    [[nodiscard]] Scope scope() const;

    /// @return First row the selection touches, which may be above the screen.
    [[nodiscard]] std::ptrdiff_t firstRow() const;

    /// @return Last row the selection touches, which may be below the screen.
    [[nodiscard]] std::ptrdiff_t lastRow() const;

private:
    bool m_active{false};                ///< Whether the mouse picked something out.
    Scope m_scope{Scope::Conversation};  ///< Where the selection lives.
    std::ptrdiff_t m_anchorRow{0};       ///< Row where the button went down.
    std::size_t m_anchorColumn{0};       ///< Column where the button went down.
    std::ptrdiff_t m_cursorRow{0};       ///< Row the mouse is at.
    std::size_t m_cursorColumn{0};       ///< Column the mouse is at.
};

/// Reads what a selection covers off the frame that was painted.
///
/// Only what was said is copied: the layout the client draws to place it is left out, which
/// means the frame, the prompt marker, the indentation before a name, the blank rows between
/// turns and the empty columns of a panel. The right half of a wide character contributes
/// nothing either, because the character itself already covers the columns. Rows are joined by
/// line breaks and trailing blanks are dropped, and only the rows of the scope are read: a
/// selection of the conversation never reads the prompt, and one of the prompt never reads the
/// conversation.
/// @param frame Frame the selection was made on.
/// @param selection Region to read.
/// @param rows Rows of the screen the scope of the selection takes.
/// @return The text, empty when nothing is covered.
[[nodiscard]] std::string selectedText(const Canvas& frame, const Selection& selection,
                                       Bounds rows);

/// The word under a position, the way a terminal picks one out on a double click.
///
/// Letters, digits and underscores run together, and so does anything that is not ASCII. A cell
/// that is not part of a word is a word of its own, so a double click on a comma takes the comma.
/// @param frame Frame that was painted.
/// @param row Row the pointer is on.
/// @param column Column the pointer is on.
/// @param scope Where the row is, which the selection it returns belongs to.
/// @return The selection that covers the word, empty when the position is off the frame.
// As everywhere here: a row and a column, both named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] Selection wordAt(const Canvas& frame, std::size_t row, std::size_t column,
                               Scope scope = Scope::Conversation);

/// The whole row under a position, the way a terminal picks one out on a triple click.
/// @param frame Frame that was painted.
/// @param row Row the pointer is on.
/// @param scope Where the row is, which the selection it returns belongs to.
/// @return The selection that covers the row, empty when the position is off the frame.
[[nodiscard]] Selection lineAt(const Canvas& frame, std::size_t row,
                               Scope scope = Scope::Conversation);

/// Whether a cell is layout rather than something that was said.
///
/// Layout is what the client draws to place the words: the panel the prompt sits on, the strokes
/// of its frame, the marker in front of it and the scrollbar on the right. A selection neither
/// paints it nor copies it, so the screen looks picked out only where there is something to read.
/// @param frame Frame that was painted, for the width of its last column.
/// @param cell Cell to look at.
/// @param column Column the cell is in.
/// @return True for a cell that holds no words.
[[nodiscard]] bool isLayout(const Canvas& frame, const Cell& cell, std::size_t column);

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
