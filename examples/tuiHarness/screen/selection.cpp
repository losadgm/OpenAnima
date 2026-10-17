#include "selection.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <optional>
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
/// @param row Row, which is below zero for a selection that has scrolled out of the viewport.
/// @param column Column.
/// @return A number that grows down the screen and to the right.
long long rank(std::ptrdiff_t row, std::size_t column) {
    constexpr long long ColumnsPerRow = 1LL << 20LL;
    return (row * ColumnsPerRow) + static_cast<long long>(column);
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

/// @param cell Cell to look at.
/// @return Whether it is a stroke of the frame the client draws around the prompt, which is a
///         stroke painted on the panel and nowhere else: the same character in what was said is
///         part of what was said.
bool isBoxGlyph(const Cell& cell) {
    if (cell.kind != Cell::Kind::Glyph || !cell.style.background.equals(Palette::panelColor())) {
        return false;
    }
    const std::string_view glyph{cell.glyph.data(), cell.length};
    return glyph == Glyphs::BoxTopLeft || glyph == Glyphs::BoxTop || glyph == Glyphs::BoxTopRight ||
           glyph == Glyphs::BoxLeft || glyph == Glyphs::BoxRight ||
           glyph == Glyphs::BoxBottomLeft || glyph == Glyphs::BoxBottom ||
           glyph == Glyphs::BoxBottomRight;
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
    if (glyph == Glyphs::PromptMarker || isBoxGlyph(cell)) {
        return true;
    }
    return column + 1 == frame.width() && glyph == Glyphs::ScrollBar;
}

}  // namespace

bool isLayout(const Canvas& frame, const Cell& cell, std::size_t column) {
    return cell.furniture || isFurniture(frame, cell, column);
}

std::size_t Bounds::end() const {
    return first + count;
}

bool Bounds::empty() const {
    return count == 0;
}

Bounds boundsOf(Scope scope, std::size_t scrollRows, std::size_t height) {
    const std::size_t split = std::min(scrollRows, height);
    if (scope == Scope::Conversation) {
        return Bounds{0, split};
    }
    return Bounds{split, height - split};
}

std::optional<RowSpan> coveredRows(const Selection& selection, Bounds rows, std::size_t height) {
    if (selection.empty() || rows.empty() || height == 0 || selection.lastRow() < 0) {
        return std::nullopt;
    }
    const std::size_t first = std::max(
        static_cast<std::size_t>(std::max<std::ptrdiff_t>(selection.firstRow(), 0)), rows.first);
    const std::size_t last =
        std::min(static_cast<std::size_t>(std::max<std::ptrdiff_t>(selection.lastRow(), 0)),
                 std::min(rows.end(), height) - 1);
    if (first > last) {
        return std::nullopt;
    }
    return RowSpan{first, last};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Selection::begin(std::size_t row, std::size_t column, Scope scope) {
    m_active = true;
    m_scope = scope;
    m_anchorRow = static_cast<std::ptrdiff_t>(row);
    m_anchorColumn = column;
    m_cursorRow = static_cast<std::ptrdiff_t>(row);
    m_cursorColumn = column;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Selection::extend(std::size_t row, std::size_t column) {
    if (!m_active) {
        return;
    }
    m_cursorRow = static_cast<std::ptrdiff_t>(row);
    m_cursorColumn = column;
}

void Selection::clear() {
    m_active = false;
}

void Selection::shift(std::ptrdiff_t rows) {
    if (!m_active || m_scope == Scope::Screen) {
        return;
    }
    // The rows are kept as they are, even when they leave the viewport: the span belongs to the
    // text, and it is there again as soon as the text is.
    slide(rows);
}

void Selection::slide(std::ptrdiff_t rows) {
    if (!m_active) {
        return;
    }
    m_anchorRow += rows;
    m_cursorRow += rows;
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
    const long long here = rank(static_cast<std::ptrdiff_t>(row), column);
    const long long anchor = rank(m_anchorRow, m_anchorColumn);
    const long long cursor = rank(m_cursorRow, m_cursorColumn);
    return here >= std::min(anchor, cursor) && here <= std::max(anchor, cursor);
}

Scope Selection::scope() const {
    return m_scope;
}

std::ptrdiff_t Selection::firstRow() const {
    return std::min(m_anchorRow, m_cursorRow);
}

std::ptrdiff_t Selection::lastRow() const {
    return std::max(m_anchorRow, m_cursorRow);
}

std::string selectedText(const Canvas& frame, const Selection& selection, Bounds rows) {
    const std::optional<RowSpan> span = coveredRows(selection, rows, frame.height());
    if (!span.has_value()) {
        return {};
    }

    std::string text;
    for (std::size_t row = span->first; row <= span->last; ++row) {
        std::string line;
        for (std::size_t column = 0; column < frame.width(); ++column) {
            if (!selection.contains(row, column)) {
                continue;
            }
            const Cell& cell = frame.at(row, column);
            // Layout is what the client drew to place the words: it is skipped here, so a copy
            // reads as what was said and not as the screen it was said on.
            if (cell.kind == Cell::Kind::Continuation || isLayout(frame, cell, column)) {
                continue;
            }
            if (cell.kind == Cell::Kind::Blank) {
                line += ' ';  // A space that was painted is part of the text.
                continue;
            }
            line.append(cell.glyph.data(), cell.length);
        }
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;  // A row of layout, which is not a line of anything.
        }
        if (!text.empty()) {
            text += '\n';
        }
        text += line;
    }
    return text;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Selection wordAt(const Canvas& frame, std::size_t row, std::size_t column, Scope scope) {
    Selection selection;
    if (row >= frame.height() || column >= frame.width()) {
        return selection;
    }

    selection.begin(row, column, scope);
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
    selection.begin(row, first, scope);
    selection.extend(row, last);
    return selection;
}

Selection lineAt(const Canvas& frame, std::size_t row, Scope scope) {
    Selection selection;
    if (row >= frame.height() || frame.width() == 0) {
        return selection;
    }
    selection.begin(row, 0, scope);
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
