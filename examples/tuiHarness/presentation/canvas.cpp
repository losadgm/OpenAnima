#include "canvas.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "cell.h"
#include "text.h"

namespace OpenAnimaTui::Presentation {

namespace {

/// Copies a range of bytes into a cell that is about to hold a character.
void setGlyph(Cell& cell, std::string_view bytes, bool wide) {
    cell.kind = Cell::Kind::Glyph;
    cell.length = static_cast<std::uint8_t>(bytes.size());
    cell.wide = wide;
    cell.furniture = false;
    std::copy(bytes.begin(), bytes.end(), cell.glyph.begin());
}

/// Blanks a cell, keeping the style it is painted with.
void setBlank(Cell& cell) {
    cell.kind = Cell::Kind::Blank;
    cell.length = 0;
    cell.wide = false;
    cell.furniture = true;
}

/// Whether what starts at a position is a character whose remaining bytes have not arrived.
/// @param text Text being painted.
/// @param offset Position of the first byte.
/// @return True for half a character, which is what a draft holds between two keystrokes and
/// what a streamed answer holds between two pieces.
bool incompleteAt(std::string_view text, std::size_t offset) {
    const std::size_t wanted = utf8Length(static_cast<unsigned char>(text[offset]));
    return wanted > 1 && offset + wanted > text.size();
}

}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Canvas::Canvas(std::size_t width, std::size_t height) : m_width{width}, m_height{height} {
    m_cells.resize(m_width * m_height);
}

void Canvas::resize(std::size_t width, std::size_t height) {
    Canvas resized{width, height};
    const std::size_t rows = (height < m_height) ? height : m_height;
    const std::size_t columns = (width < m_width) ? width : m_width;
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t column = 0; column < columns; ++column) {
            resized.m_cells[resized.index(row, column)] = m_cells[index(row, column)];
        }
    }
    *this = std::move(resized);
}

std::size_t Canvas::width() const noexcept {
    return m_width;
}

std::size_t Canvas::height() const noexcept {
    return m_height;
}

void Canvas::clear(const Style& style) {
    for (Cell& cell : m_cells) {
        setBlank(cell);
        cell.style = style;
    }
}

std::size_t Canvas::index(std::size_t row, std::size_t column) const {
    return (row * m_width) + column;
}

const Cell& Canvas::at(std::size_t row, std::size_t column) const {
    assert(row < m_height && column < m_width);
    return m_cells[index(row, column)];
}

Cell& Canvas::at(std::size_t row, std::size_t column) {
    assert(row < m_height && column < m_width);
    return m_cells[index(row, column)];
}

void Canvas::detach(std::size_t row, std::size_t column) {
    Cell& cell = m_cells[index(row, column)];
    if (cell.kind == Cell::Kind::Continuation) {
        if (column > 0) {
            setBlank(m_cells[index(row, column - 1)]);
        }
        setBlank(cell);
        return;
    }
    if (cell.kind == Cell::Kind::Glyph && cell.wide) {
        const std::size_t next = column + 1;
        if (next < m_width) {
            setBlank(m_cells[index(row, next)]);
        }
        setBlank(cell);
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Canvas::put(std::size_t row, std::size_t column, std::string_view text, const Style& style) {
    if (row >= m_height) {
        return;
    }

    std::size_t current = column;
    for (std::size_t offset = 0; offset < text.size();) {
        const std::size_t start = offset;
        // Half a character is not a character yet: painting it would send the bytes to the
        // terminal, which turns them into a glyph of its own, in a cell the client believes is
        // blank and so never paints over. It waits for the rest.
        if (incompleteAt(text, offset)) {
            return;
        }
        const std::uint32_t codepoint = decodeCodepoint(text, offset);
        const std::string_view bytes = text.substr(start, offset - start);

        if (codepoint == '\n') {
            ++row;
            if (row >= m_height) {
                return;
            }
            current = column;
            continue;
        }
        if (codepoint == '\r') {
            current = column;
            continue;
        }

        const std::size_t columns = glyphColumns(codepoint);
        if (columns == 0) {
            // A combining mark joins the character before it, and is dropped when there is none.
            if (current > 0) {
                (void)m_cells[index(row, current - 1)].appendCombining(bytes);
            }
            continue;
        }
        if (current + columns > m_width) {
            // Nothing fits here, and a wide character is not split: the rest of the row is left
            // alone rather than shown wrong.
            return;
        }

        for (std::size_t index = 0; index < columns; ++index) {
            detach(row, current + index);
        }
        if (codepoint == ' ') {
            // A space is a blank cell with a style: saying it another way would make two frames
            // that look the same compare different, and every one of those is a repaint.
            setBlank(m_cells[index(row, current)]);
        } else {
            setGlyph(m_cells[index(row, current)], bytes, columns == 2);
        }
        // What was painted is what was said, so a space inside a line is text and not layout.
        m_cells[index(row, current)].furniture = false;
        m_cells[index(row, current)].style = style;
        if (columns == 2) {
            Cell& next = m_cells[index(row, current + 1)];
            next.kind = Cell::Kind::Continuation;
            next.furniture = false;
            next.style = style;
        }
        current += columns;
    }
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void Canvas::fill(std::size_t row, std::size_t column, std::size_t columns, const Style& style) {
    if (row >= m_height) {
        return;
    }
    const std::size_t end = (column + columns < m_width) ? (column + columns) : m_width;
    for (std::size_t current = column; current < end; ++current) {
        detach(row, current);
        Cell& cell = m_cells[index(row, current)];
        // A fill is a floor to paint on, so whatever was there goes: a panel laid over the
        // conversation covers the words behind it instead of letting them show through.
        setBlank(cell);
        cell.style = style;
        cell.furniture = true;
    }
}

bool Canvas::rowEquals(const Canvas& other, std::size_t row) const {
    assert(row < m_height && row < other.m_height && m_width == other.m_width);
    const std::size_t start = row * m_width;
    for (std::size_t column = 0; column < m_width; ++column) {
        if (!m_cells[start + column].equals(other.m_cells[start + column])) {
            return false;
        }
    }
    return true;
}

}  // namespace OpenAnimaTui::Presentation
