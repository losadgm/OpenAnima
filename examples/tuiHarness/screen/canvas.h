/// @file
/// @brief The screen as a grid of cells, which is what makes a frame comparable.

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "cell.h"
#include "style.h"

namespace OpenAnimaTui::Screen {

/// A rectangle of cells: the only thing the client paints on.
///
/// Nothing here writes to a terminal and nothing here builds an escape sequence, so the
/// whole thing is decided and compared before a byte leaves the process.
class Canvas {
public:
    /// An empty grid, which is what a frame starts as before it is given a size.
    Canvas() = default;

    /// @param width Columns.
    /// @param height Rows.
    // A width and a height are both counts, and naming the type that holds them would put a
    // struct in front of every screen in the client for nothing.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    Canvas(std::size_t width, std::size_t height);

    /// Grows or shrinks the grid, keeping what fits and blanking the rest.
    /// @param width Columns.
    /// @param height Rows.
    void resize(std::size_t width, std::size_t height);

    /// @return Columns.
    [[nodiscard]] std::size_t width() const noexcept;

    /// @return Rows.
    [[nodiscard]] std::size_t height() const noexcept;

    /// Blanks every cell.
    /// @param style Style the blank cells are painted with, the panel background for instance.
    void clear(const Style& style = Style{});

    /// The cell at a position.
    /// @param row Row, inside the grid.
    /// @param column Column, inside the grid.
    /// @return The cell.
    [[nodiscard]] const Cell& at(std::size_t row, std::size_t column) const;

    /// Writes text at a position, clipping what does not fit.
    ///
    /// Code points are never split, a wide character that does not fit in the column is
    /// dropped, and replacing half of a wide character blanks the other half so the grid never
    /// holds an orphan.
    /// @param row Row to write in.
    /// @param column Column to start at.
    /// @param text Text to write, which may hold line breaks.
    /// @param style Style to paint it with.
    // A position on a grid is a row and a column, and they are named at every call site.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void put(std::size_t row, std::size_t column, std::string_view text, const Style& style);

    /// Blanks a span of a row, which is how a background panel is laid down.
    /// @param row Row to fill.
    /// @param column First column.
    /// @param columns Columns to fill.
    /// @param style Style the blank cells are painted with.
    // As above: a row, a column and a count, each named where it is used.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void fill(std::size_t row, std::size_t column, std::size_t columns, const Style& style);

    /// @param other Canvas to compare against, which has to have the same size.
    /// @param row Row to compare.
    /// @return Whether every cell of the row is painted the same in both.
    [[nodiscard]] bool rowEquals(const Canvas& other, std::size_t row) const;

private:
    /// Where a position lands in the flat grid.
    /// @param row Row, inside the grid.
    /// @param column Column, inside the grid.
    /// @return The index of the cell.
    [[nodiscard]] std::size_t index(std::size_t row, std::size_t column) const;

    /// Makes sure the cells of a position are free to be written: a half of a wide character
    /// left behind would be shown as a stray column.
    void detach(std::size_t row, std::size_t column);

    std::size_t m_width{0};   ///< Columns.
    std::size_t m_height{0};  ///< Rows.
    std::vector<Cell> m_cells;
};

}  // namespace OpenAnimaTui::Screen
