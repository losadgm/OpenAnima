#include "diff.h"

#include <cstddef>
#include <string>

#include "canvas.h"
#include "cell.h"
#include "style.h"

namespace OpenAnimaTui::Presentation {

namespace {

/// The ways a terminal is asked for things, in one place.
constexpr const char* CursorHome = "\x1b[H";
constexpr const char* EraseDisplay = "\x1b[2J";
constexpr const char* EraseToEndOfLine = "\x1b[K";
constexpr const char* Reset = "\x1b[0m";
constexpr const char* ShowCaret = "\x1b[?25h";
constexpr const char* HideCaret = "\x1b[?25l";

/// A gap between two changed cells shorter than this is written over rather than jumped to:
/// moving the cursor costs about as much as these cells do.
constexpr std::size_t BridgeGap = 8;

/// Appends a decimal number.
void appendNumber(std::string& out, std::size_t value) {
    out += std::to_string(value);
}

/// Appends one colour as SGR parameters.
void appendColor(std::string& out, const Color& color, bool foreground) {
    switch (color.kind) {
    case Color::Kind::Default:
        out += foreground ? "39" : "49";
        return;
    case Color::Kind::Indexed:
        out += foreground ? "38;5;" : "48;5;";
        appendNumber(out, color.index);
        return;
    case Color::Kind::Rgb:
        out += foreground ? "38;2;" : "48;2;";
        appendNumber(out, color.red);
        out += ';';
        appendNumber(out, color.green);
        out += ';';
        appendNumber(out, color.blue);
        return;
    }
}

}  // namespace

void FrameWriter::forget() {
    m_started = false;
    m_cursorKnown = false;
    m_current = Style{};
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void FrameWriter::setCaret(std::size_t row, std::size_t column) {
    m_caretWanted = true;
    m_caretRow = row;
    m_caretColumn = column;
}

bool FrameWriter::changed(const Canvas& frame, std::size_t row, std::size_t column) const {
    if (!m_started) {
        return true;
    }
    return !frame.at(row, column).equals(m_previous.at(row, column));
}

void FrameWriter::moveTo(std::size_t row, std::size_t column, std::string& out) {
    if (m_cursorKnown && m_cursorRow == row && m_cursorColumn == column) {
        return;
    }
    out += "\x1b[";
    appendNumber(out, row + 1);
    out += ';';
    appendNumber(out, column + 1);
    out += 'H';
    m_cursorRow = row;
    m_cursorColumn = column;
    m_cursorKnown = true;
}

void FrameWriter::paint(const Style& style, std::string& out) {
    if (style.equals(m_current)) {
        return;
    }
    // A reset first leaves the terminal exactly as the parameters that follow say it is, which
    // is what makes comparing the next cell against it safe.
    out += Reset;
    if (!style.isDefault()) {
        std::string parameters;
        appendColor(parameters, style.foreground, true);
        parameters += ';';
        appendColor(parameters, style.background, false);
        if (style.bold) {
            parameters += ";1";
        }
        if (style.dim) {
            parameters += ";2";
        }
        if (style.italic) {
            parameters += ";3";
        }
        if (style.underline) {
            parameters += ";4";
        }
        if (style.reverse) {
            parameters += ";7";
        }
        out += "\x1b[";
        out += parameters;
        out += 'm';
    }
    m_current = style;
}

void FrameWriter::writeCell(const Cell& cell, std::size_t width, std::size_t& column,
                            std::string& out) {
    if (cell.kind == Cell::Kind::Continuation) {
        // The character to the left already covered this cell.
        ++column;
        return;
    }
    if (cell.kind == Cell::Kind::Blank) {
        out += ' ';
    } else {
        out.append(cell.glyph.data(), cell.length);
    }
    const std::size_t size = cell.wide ? 2 : 1;
    if (m_cursorKnown) {
        m_cursorColumn += size;
        if (m_cursorColumn >= width) {
            // Having written the last column leaves the terminal about to wrap, and that is a
            // state worth assuming nothing about.
            m_cursorKnown = false;
        }
    }
    column += size;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
FrameWriter::Span FrameWriter::nextSpan(const Canvas& frame, std::size_t row,
                                        std::size_t from) const {
    const std::size_t width = frame.width();
    // The first cell after `from` that differs, never inside a wide character.
    std::size_t start = from;
    while (start < width && !changed(frame, row, start)) {
        ++start;
    }
    Span span;
    if (start >= width) {
        return span;
    }
    if (start > 0 && frame.at(row, start).kind == Cell::Kind::Continuation) {
        --start;
    }

    // The end of that run, bridging the small gaps: moving the cursor costs about as much as the
    // cells it skips.
    std::size_t end = start + 1;
    std::size_t scan = start;
    while (scan < width) {
        if (changed(frame, row, scan)) {
            end = scan + 1;
            ++scan;
            continue;
        }
        std::size_t gap = scan;
        while (gap < width && !changed(frame, row, gap)) {
            ++gap;
        }
        if (gap >= width || gap - scan > BridgeGap) {
            break;
        }
        scan = gap;
    }
    if (end < width && frame.at(row, end).kind == Cell::Kind::Continuation &&
        frame.at(row, end - 1).wide) {
        ++end;
    }
    span.start = start;
    span.end = end;
    return span;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
bool FrameWriter::hasBlankTail(const Canvas& frame, std::size_t row, std::size_t from) {
    for (std::size_t index = from; index < frame.width(); ++index) {
        const Cell& cell = frame.at(row, index);
        if (cell.kind != Cell::Kind::Blank || !cell.style.isDefault()) {
            return false;
        }
    }
    return true;
}

void FrameWriter::writeSpan(const Canvas& frame, std::size_t row, Span span, std::string& out) {
    std::size_t position = span.start;
    while (position < span.end) {
        const Cell& cell = frame.at(row, position);
        if (cell.kind == Cell::Kind::Continuation) {
            ++position;
            continue;
        }
        paint(cell.style, out);
        writeCell(cell, frame.width(), position, out);
    }
}

void FrameWriter::emitRow(const Canvas& frame, std::size_t row, std::string& out) {
    std::size_t column = 0;
    while (column < frame.width()) {
        const Span span = nextSpan(frame, row, column);
        if (span.end == 0) {
            return;
        }
        moveTo(row, span.start, out);
        writeSpan(frame, row, span, out);
        if (hasBlankTail(frame, row, span.end) && span.end < frame.width()) {
            // Erasing paints with the background in force, so it is set first. It does not move
            // the cursor, and what it clears is blank either way.
            paint(Style{}, out);
            out += EraseToEndOfLine;
            return;
        }
        column = span.end;
    }
}

void FrameWriter::emitAll(const Canvas& frame, std::string& out) {
    out += EraseDisplay;
    out += CursorHome;
    m_cursorRow = 0;
    m_cursorColumn = 0;
    m_cursorKnown = true;
    for (std::size_t row = 0; row < frame.height(); ++row) {
        moveTo(row, 0, out);
        std::size_t column = 0;
        while (column < frame.width()) {
            const Cell& cell = frame.at(row, column);
            if (cell.kind == Cell::Kind::Continuation) {
                ++column;
                continue;
            }
            paint(cell.style, out);
            writeCell(cell, frame.width(), column, out);
        }
    }
}

void FrameWriter::emit(const Canvas& frame, std::string& out) {
    const bool sameSize =
        m_started && frame.width() == m_previous.width() && frame.height() == m_previous.height();
    if (sameSize) {
        for (std::size_t row = 0; row < frame.height(); ++row) {
            if (!frame.rowEquals(m_previous, row)) {
                emitRow(frame, row, out);
            }
        }
    } else {
        emitAll(frame, out);
    }
    m_previous = frame;
    m_started = true;

    if (m_caretWanted && m_caretRow < frame.height() && m_caretColumn < frame.width()) {
        moveTo(m_caretRow, m_caretColumn, out);
        if (!m_caretShown) {
            out += ShowCaret;
            m_caretShown = true;
        }
    } else if (m_caretShown) {
        out += HideCaret;
        m_caretShown = false;
    }
    m_caretWanted = false;
}

}  // namespace OpenAnimaTui::Presentation
