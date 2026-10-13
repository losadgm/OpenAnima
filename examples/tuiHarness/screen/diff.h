/// @file
/// @brief Turning a canvas into bytes: only what changed, and only when it changed.

#pragma once

#include <cstddef>
#include <string>

#include "canvas.h"
#include "style.h"

namespace OpenAnimaTui::Screen {

/// Writes a frame to a terminal by comparing it with the one before it.
///
/// The client paints a whole canvas every time, and this decides what that costs: a row that
/// did not change is not written at all, a cell that did not change is not written, the cursor
/// is only moved when the next cell is not where it already is, and a style is only sent when
/// it differs from the one the terminal is in. Painting a screen that looks the same as the
/// last one costs nothing, which is what makes the loop free to run often.
///
/// It also keeps the caret honest: a write that ends on the last column leaves the terminal
/// waiting to wrap, which is a state no assumption survives, so the position is forgotten
/// there and asked for again when it is needed.
class FrameWriter {
public:
    /// Appends the escape sequences that turn the terminal from the previous frame into this
    /// one. The first frame, and any frame after a resize, paints everything.
    /// @param frame Canvas to show.
    /// @param out Buffer the bytes are appended to.
    void emit(const Canvas& frame, std::string& out);

    /// Asks for the terminal caret to be left at a position once the frame is written.
    ///
    /// It is where the caret has to be while a prompt is being typed, so it goes last, after
    /// the cells, and the shell does not have to know where the prompt ended up.
    /// @param row Row, inside the canvas.
    /// @param column Column, inside the canvas.
    // A position on a grid is a row and a column, and they are named at both ends of the call.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void setCaret(std::size_t row, std::size_t column);

    /// Throws away what is known about the terminal, so the next frame paints everything.
    ///
    /// The client calls it after leaving the alternate screen or after writing to the terminal
    /// itself, both of which make the shadow guess wrong.
    void forget();

private:
    /// A run of a row that has to be written.
    struct Span {
        std::size_t start{0};  ///< First column of the run.
        std::size_t end{0};    ///< One past the last column, zero when there is no run.
    };

    /// Paints every cell of the frame.
    void emitAll(const Canvas& frame, std::string& out);

    /// Paints the cells of a row that differ from the previous frame.
    void emitRow(const Canvas& frame, std::size_t row, std::string& out);

    /// Finds the next run of a row that has to be written.
    /// @param frame Frame being written.
    /// @param row Row to look at.
    /// @param from Column to start looking at.
    /// @return The run, with `end` zero when the rest of the row is unchanged.
    // A row and a column to start from, both named at the call site.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] Span nextSpan(const Canvas& frame, std::size_t row, std::size_t from) const;

    /// @param frame Frame being written.
    /// @param row Row to look at.
    /// @param from Column to start at.
    /// @return Whether the rest of the row is blank, which one erase can say.
    // A row and a column to start from, both named at the call site.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    [[nodiscard]] static bool hasBlankTail(const Canvas& frame, std::size_t row, std::size_t from);

    /// Writes the cells of a run.
    /// @param frame Frame being written.
    /// @param row Row being written.
    /// @param span Columns to write.
    /// @param out Buffer the bytes are appended to.
    void writeSpan(const Canvas& frame, std::size_t row, Span span, std::string& out);

    /// Moves the terminal cursor, unless it is already there.
    void moveTo(std::size_t row, std::size_t column, std::string& out);

    /// Tells the terminal how to paint, unless it is already painting that way.
    void paint(const Style& style, std::string& out);

    /// Writes the character of a cell and moves the column past it.
    /// @param cell Cell to write.
    /// @param width Columns of the frame, so the way the terminal wraps is accounted for.
    /// @param column Column being written, moved past the character.
    /// @param out Buffer the bytes are appended to.
    void writeCell(const Cell& cell, std::size_t width, std::size_t& column, std::string& out);

    /// @param frame Frame being written.
    /// @param row Row to look at.
    /// @param column Column to look at.
    /// @return Whether the cell differs from the previous frame, or the frame is the first.
    [[nodiscard]] bool changed(const Canvas& frame, std::size_t row, std::size_t column) const;

    Canvas m_previous;          ///< What the terminal is showing, as far as this knows.
    bool m_started{false};      ///< Whether a frame was written before.
    bool m_cursorKnown{false};  ///< Whether the position of the terminal cursor is known.
    std::size_t m_cursorRow{0};
    std::size_t m_cursorColumn{0};
    Style m_current{};  ///< Style the terminal is painting with.
    bool m_caretWanted{false};
    bool m_caretShown{false};  ///< Whether the terminal caret is currently visible.
    std::size_t m_caretRow{0};
    std::size_t m_caretColumn{0};
};

}  // namespace OpenAnimaTui::Screen
