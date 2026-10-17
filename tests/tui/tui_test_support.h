/// @file
/// @brief Reading a canvas the way a terminal would, for the screen tests.

#pragma once

#include <cstddef>
#include <string>

#include "presentation/canvas.h"
#include "presentation/cell.h"
#include "presentation/style.h"

namespace OpenAnimaTui::Tests {

/// The aliases the screen tests use, so a case reads like the code it checks.
namespace Palette = OpenAnimaTui::Presentation::Palette;
using OpenAnimaTui::Presentation::Canvas;
using OpenAnimaTui::Presentation::Cell;
using OpenAnimaTui::Presentation::Color;
using OpenAnimaTui::Presentation::Style;

/// The characters of a row, as they would be read off the screen.
/// @param canvas Canvas to read.
/// @param row Row to read.
/// @return The text, continuation cells skipped.
[[nodiscard]] inline std::string readRow(const Canvas& canvas, std::size_t row) {
    std::string text;
    for (std::size_t column = 0; column < canvas.width(); ++column) {
        const Cell& cell = canvas.at(row, column);
        if (cell.kind == Cell::Kind::Continuation) {
            continue;
        }
        if (cell.kind == Cell::Kind::Blank) {
            text += ' ';
            continue;
        }
        text.append(cell.glyph.data(), cell.length);
    }
    return text;
}

/// The text of every row, joined with line breaks.
/// @param canvas Canvas to read.
/// @return The screen as text.
[[nodiscard]] inline std::string readAll(const Canvas& canvas) {
    std::string text;
    for (std::size_t row = 0; row < canvas.height(); ++row) {
        text += readRow(canvas, row);
        text += '\n';
    }
    return text;
}

}  // namespace OpenAnimaTui::Tests
