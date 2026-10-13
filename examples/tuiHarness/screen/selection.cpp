#include "selection.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "canvas.h"
#include "cell.h"

namespace OpenAnimaTui::Screen {

namespace {

/// Packs a position into one number, so two of them can be ordered the way a reader would.
/// @param row Row.
/// @param column Column.
/// @return A number that grows down the screen and to the right.
std::size_t rank(std::size_t row, std::size_t column) {
    constexpr std::size_t ColumnsPerRow = 1U << 20U;
    return (row * ColumnsPerRow) + column;
}

}  // namespace

void Selection::begin(std::size_t row, std::size_t column) {
    m_active = true;
    m_anchorRow = row;
    m_anchorColumn = column;
    m_cursorRow = row;
    m_cursorColumn = column;
}

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
    return !m_active || (m_anchorRow == m_cursorRow && m_anchorColumn == m_cursorColumn);
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
            if (cell.kind == Cell::Kind::Continuation) {
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

}  // namespace OpenAnimaTui::Screen
