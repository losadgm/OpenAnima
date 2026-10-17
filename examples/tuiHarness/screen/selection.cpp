#include "selection.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

#include "canvas.h"
#include "cell.h"
#include "style.h"

namespace OpenAnimaTui::Screen {

namespace {

/// How long two presses in the same cell still count as one double click.
constexpr std::chrono::milliseconds DoubleClickTime{400};

/// Packs a position into one number, so two of them can be ordered the way a reader would.
/// @param row Row.
/// @param column Column.
/// @return A number that grows down the screen and to the right.
std::size_t rank(std::size_t row, std::size_t column) {
    constexpr std::size_t ColumnsPerRow = 1U << 20U;
    return (row * ColumnsPerRow) + column;
}

/// @param cell Cell to look at.
/// @return Whether the cell holds a character that runs together with the one beside it: a
///         letter, a digit, an underscore, or a byte that is not ASCII.
bool isWordCell(const Cell& cell) {
    if (cell.kind == Cell::Kind::Continuation) {
        return true;  // The right half of a wide character, which is one of the word.
    }
    if (cell.kind != Cell::Kind::Glyph || cell.length == 0) {
        return false;
    }
    const auto first = static_cast<unsigned char>(cell.glyph.front());
    if (first >= 0x80U) {
        return true;  // Not ASCII: a letter of some other script.
    }
    return std::isalnum(first) != 0 || first == '_';
}

/// @param frame Frame that was painted, for the width of its last column.
/// @param cell Cell to look at.
/// @param column Column the cell is in.
/// @return Whether the cell is what the client drew for itself rather than what was said.
bool isFurniture(const Canvas& frame, const Cell& cell, std::size_t column) {
    if (cell.kind != Cell::Kind::Glyph) {
        return false;
    }
    const std::string_view glyph{cell.glyph.data(), cell.length};
    if (glyph == Glyphs::PromptMarker) {
        return true;
    }
    return column + 1 == frame.width() && glyph == Glyphs::ScrollBar;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Selection::begin(std::size_t row, std::size_t column) {
    m_active = true;
    m_anchorRow = row;
    m_anchorColumn = column;
    m_cursorRow = row;
    m_cursorColumn = column;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Selection::extend(std::size_t row, std::size_t column) {
    if (!m_active) {
        return;
    }
    m_cursorRow = row;
    m_cursorColumn = column;
}

void Selection::clear() {
    m_active = false;
}

bool Selection::active() const {
    return m_active;
}

bool Selection::empty() const {
    return !m_active;
}

bool Selection::contains(std::size_t row, std::size_t column) const {
    if (!m_active) {
        return false;
    }
    const std::size_t here = rank(row, column);
    const std::size_t anchor = rank(m_anchorRow, m_anchorColumn);
    const std::size_t cursor = rank(m_cursorRow, m_cursorColumn);
    return here >= std::min(anchor, cursor) && here <= std::max(anchor, cursor);
}

std::size_t Selection::firstRow() const {
    return std::min(m_anchorRow, m_cursorRow);
}

std::size_t Selection::lastRow() const {
    return std::max(m_anchorRow, m_cursorRow);
}

std::string selectedText(const Canvas& frame, const Selection& selection) {
    if (selection.empty()) {
        return {};
    }

    std::string text;
    const std::size_t last = std::min(selection.lastRow(), frame.height() - 1);
    for (std::size_t row = selection.firstRow(); row <= last; ++row) {
        std::string line;
        for (std::size_t column = 0; column < frame.width(); ++column) {
            if (!selection.contains(row, column)) {
                continue;
            }
            const Cell& cell = frame.at(row, column);
            if (cell.kind == Cell::Kind::Continuation || isFurniture(frame, cell, column)) {
                continue;
            }
            if (cell.kind == Cell::Kind::Blank) {
                line += ' ';
                continue;
            }
            line.append(cell.glyph.data(), cell.length);
        }
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
        if (row > selection.firstRow()) {
            text += '\n';
        }
        text += line;
    }
    return text;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Selection wordAt(const Canvas& frame, std::size_t row, std::size_t column) {
    Selection selection;
    if (row >= frame.height() || column >= frame.width()) {
        return selection;
    }

    selection.begin(row, column);
    if (!isWordCell(frame.at(row, column))) {
        return selection;
    }

    std::size_t first = column;
    while (first > 0 && isWordCell(frame.at(row, first - 1))) {
        --first;
    }
    std::size_t last = column;
    while (last + 1 < frame.width() && isWordCell(frame.at(row, last + 1))) {
        ++last;
    }
    selection.begin(row, first);
    selection.extend(row, last);
    return selection;
}

Selection lineAt(const Canvas& frame, std::size_t row) {
    Selection selection;
    if (row >= frame.height() || frame.width() == 0) {
        return selection;
    }
    selection.begin(row, 0);
    selection.extend(row, frame.width() - 1);
    return selection;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int Clicks::press(std::size_t row, std::size_t column, std::chrono::steady_clock::time_point when) {
    const bool sameCell = row == m_row && column == m_column;
    const bool soon = m_count > 0 && when - m_at <= DoubleClickTime;
    m_count = sameCell && soon ? m_count + 1 : 1;
    m_at = when;
    m_row = row;
    m_column = column;
    return m_count;
}

}  // namespace OpenAnimaTui::Screen
