/// @file
/// @brief The frame writer, checked by pretending to be the terminal it writes to.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "screen/canvas.h"
#include "screen/cell.h"
#include "screen/diff.h"
#include "screen/style.h"
#include "screen/text.h"
#include "tui_test_support.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

namespace {

using OpenAnimaTui::Screen::FrameWriter;
using namespace OpenAnimaTui::Tests;

/// A terminal, as far as the tests care: it holds cells and moves a cursor around.
///
/// It understands exactly the sequences the writer is allowed to send, and refuses to
/// understand anything else, so a code written by mistake is a failed test and not a silence.
class Terminal {
public:
    Terminal(std::size_t width, std::size_t height) : m_width{width}, m_height{height} {
        m_cells.resize(width * height);
    }

    /// @param bytes Escape sequences and text, applied in order.
    /// @return Whether every byte belonged to a sequence understood here.
    bool apply(std::string_view bytes) {
        for (std::size_t index = 0; index < bytes.size();) {
            const char value = bytes[index];
            if (value == '\x1b') {
                if (!applyEscape(bytes, index)) {
                    return false;
                }
                continue;
            }
            if (value == '\n') {
                ++m_row;
                m_column = 0;
                ++index;
                continue;
            }
            if (value == '\r') {
                m_column = 0;
                ++index;
                continue;
            }
            const std::uint32_t codepoint = OpenAnimaTui::Screen::decodeCodepoint(bytes, index);
            const std::size_t columns = OpenAnimaTui::Screen::glyphColumns(codepoint);
            write(codepoint, columns);
        }
        return true;
    }

    /// @param row Row to read.
    /// @param column Column to read.
    /// @return The cell the terminal holds there.
    [[nodiscard]] const Cell& at(std::size_t row, std::size_t column) const {
        return m_cells[(row * m_width) + column];
    }

    /// @param canvas Canvas the terminal should be showing.
    /// @return Whether every cell of every row matches.
    [[nodiscard]] bool matches(const Canvas& canvas) const {
        if (canvas.width() != m_width || canvas.height() != m_height) {
            return false;
        }
        for (std::size_t row = 0; row < m_height; ++row) {
            for (std::size_t column = 0; column < m_width; ++column) {
                if (!at(row, column).equals(canvas.at(row, column))) {
                    return false;
                }
            }
        }
        return true;
    }

    /// Says where the terminal and a canvas first disagree, which is what a failing check
    /// needs to be read.
    /// @param canvas Canvas the terminal should be showing.
    /// @return An empty string when they match, a description of the first difference when not.
    [[nodiscard]] std::string describeDifference(const Canvas& canvas) const {
        if (canvas.width() != m_width || canvas.height() != m_height) {
            return "size " + std::to_string(canvas.width()) + "x" +
                   std::to_string(canvas.height()) + " against " + std::to_string(m_width) + "x" +
                   std::to_string(m_height);
        }
        for (std::size_t row = 0; row < m_height; ++row) {
            for (std::size_t column = 0; column < m_width; ++column) {
                if (!at(row, column).equals(canvas.at(row, column))) {
                    return "row " + std::to_string(row) + " column " + std::to_string(column) +
                           ": terminal " + describe(at(row, column)) + ", canvas " +
                           describe(canvas.at(row, column));
                }
            }
        }
        return {};
    }

    /// @param cell Cell to describe.
    /// @return Something a failure message can show.
    [[nodiscard]] static std::string describe(const Cell& cell) {
        std::string text = "[";
        if (cell.kind == Cell::Kind::Continuation) {
            text += "half of a wide character";
        } else if (cell.kind == Cell::Kind::Blank) {
            text += "blank";
        } else {
            text.append(cell.glyph.data(), cell.length);
        }
        text += "] fg ";
        text += std::to_string(cell.style.foreground.kind == OpenAnimaTui::Screen::Color::Kind::Rgb
                                   ? cell.style.foreground.red
                                   : 0);
        text += " bg ";
        text += std::to_string(cell.style.background.kind == OpenAnimaTui::Screen::Color::Kind::Rgb
                                   ? cell.style.background.red
                                   : 0);
        return text;
    }

private:
    /// @param bytes Buffer being applied.
    /// @param index Position of the escape, moved past it.
    /// @return Whether the sequence was understood.
    bool applyEscape(std::string_view bytes, std::size_t& index) {
        if (index + 1 >= bytes.size() || bytes[index + 1] != '[') {
            return false;
        }
        std::size_t cursor = index + 2;
        std::string parameters;
        while (cursor < bytes.size() && !isFinal(bytes[cursor])) {
            parameters += bytes[cursor];
            ++cursor;
        }
        if (cursor >= bytes.size()) {
            return false;
        }
        const char final = bytes[cursor];
        index = cursor + 1;

        switch (final) {
        case 'H':
            return applyCursor(parameters);
        case 'J':
            if (parameters != "2") {
                return false;
            }
            blankAll();
            m_row = 0;
            m_column = 0;
            return true;
        case 'K':
            if (!parameters.empty()) {
                return false;
            }
            eraseToEndOfLine();
            return true;
        case 'm':
            return applyStyle(parameters);
        case 'h':
        case 'l':
            return parameters == "?25";  // The caret, which a terminal is free to ignore.
        default:
            return false;
        }
    }

    /// @param value Byte to look at.
    /// @return Whether it ends a control sequence.
    [[nodiscard]] static bool isFinal(char value) {
        return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
    }

    /// @param parameters Numbers between the brackets.
    /// @return Whether the position was understood.
    bool applyCursor(const std::string& parameters) {
        if (parameters.empty()) {
            m_row = 0;
            m_column = 0;
            return true;
        }
        const std::size_t separator = parameters.find(';');
        if (separator == std::string::npos) {
            return false;
        }
        const std::size_t row =
            static_cast<std::size_t>(std::stoul(parameters.substr(0, separator)));
        const std::size_t column =
            static_cast<std::size_t>(std::stoul(parameters.substr(separator + 1)));
        if (row == 0 || column == 0 || row > m_height || column > m_width) {
            return false;
        }
        m_row = row - 1;
        m_column = column - 1;
        return true;
    }

    /// @param parameters Numbers between the brackets.
    /// @return Whether the style was understood.
    bool applyStyle(const std::string& parameters) {
        std::vector<unsigned long> values;
        std::size_t index = 0;
        while (index < parameters.size()) {
            const std::size_t end = parameters.find(';', index);
            const std::string token = parameters.substr(
                index, end == std::string::npos ? std::string::npos : end - index);
            if (!token.empty()) {
                values.push_back(std::stoul(token));
            }
            if (end == std::string::npos) {
                break;
            }
            index = end + 1;
        }

        // Attributes accumulate, which is what a terminal does too.
        Style style = m_style;
        for (std::size_t position = 0; position < values.size(); ++position) {
            switch (values[position]) {
            case 0:
                style = Style{};
                break;
            case 1:
                style = style.withBold();
                break;
            case 2:
                style = style.withDim();
                break;
            case 7:
                style = style.withReverse();
                break;
            case 39:
                style = style.withForeground(OpenAnimaTui::Screen::Color::defaultValue());
                break;
            case 49:
                style = style.withBackground(OpenAnimaTui::Screen::Color::defaultValue());
                break;
            case 38:
            case 48: {
                if (position + 4 >= values.size() || values[position + 1] != 2 ||
                    values[position + 2] > 255 || values[position + 3] > 255 ||
                    values[position + 4] > 255) {
                    return false;
                }
                const auto color = OpenAnimaTui::Screen::Color::rgb(
                    static_cast<std::uint8_t>(values[position + 2]),
                    static_cast<std::uint8_t>(values[position + 3]),
                    static_cast<std::uint8_t>(values[position + 4]));
                style = values[position] == 38 ? style.withForeground(color)
                                               : style.withBackground(color);
                position += 4;
                break;
            }
            default:
                return false;
            }
        }
        m_style = style;
        return true;
    }

    /// Writes a character where the cursor is.
    /// @param codepoint Code point to write.
    /// @param columns Columns it takes.
    // A code point and the columns it takes, named where it is called.
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
    void write(std::uint32_t codepoint, std::size_t columns) {
        if (columns == 0) {
            if (m_column > 0) {
                (void)m_cells[(m_row * m_width) + m_column - 1].appendCombining(encode(codepoint));
            }
            return;
        }
        if (m_column + columns > m_width) {
            // The terminal wraps what does not fit.
            m_row += 1;
            m_column = 0;
            if (m_row >= m_height) {
                return;
            }
        }
        Cell& cell = m_cells[(m_row * m_width) + m_column];
        cell.kind = codepoint == ' ' ? Cell::Kind::Blank : Cell::Kind::Glyph;
        cell.wide = columns == 2;
        cell.style = m_style;
        const std::string bytes = encode(codepoint);
        cell.length = codepoint == ' ' ? 0 : static_cast<std::uint8_t>(bytes.size());
        std::copy(bytes.begin(), bytes.end(), cell.glyph.begin());
        if (columns == 2 && m_column + 1 < m_width) {
            Cell& next = m_cells[(m_row * m_width) + m_column + 1];
            next.kind = Cell::Kind::Continuation;
            next.style = m_style;
        }
        m_column += columns;
        if (m_column >= m_width) {
            m_column = m_width - 1;
        }
    }

    /// @param codepoint Code point to encode.
    /// @return Its UTF-8 bytes.
    [[nodiscard]] static std::string encode(std::uint32_t codepoint) {
        std::string bytes;
        if (codepoint < 0x80) {
            bytes += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            bytes += static_cast<char>(0xC0 | (codepoint >> 6));
            bytes += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            bytes += static_cast<char>(0xE0 | (codepoint >> 12));
            bytes += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            bytes += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else {
            bytes += static_cast<char>(0xF0 | (codepoint >> 18));
            bytes += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
            bytes += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            bytes += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        return bytes;
    }

    void eraseToEndOfLine() {
        for (std::size_t column = m_column; column < m_width; ++column) {
            Cell& cell = m_cells[(m_row * m_width) + column];
            cell.kind = Cell::Kind::Blank;
            cell.length = 0;
            cell.wide = false;
            cell.style = m_style;
        }
    }

    void blankAll() {
        for (Cell& cell : m_cells) {
            cell = Cell{};
        }
    }

    std::size_t m_width{0};
    std::size_t m_height{0};
    std::vector<Cell> m_cells;
    std::size_t m_row{0};
    std::size_t m_column{0};
    Style m_style{};
};

/// One frame, applied to a terminal and compared with the canvas it came from.
void checkFrame(Terminal& terminal, FrameWriter& writer, const Canvas& canvas, std::string& bytes) {
    bytes.clear();
    writer.emit(canvas, bytes);
    REQUIRE(terminal.apply(bytes));
    const std::string difference = terminal.describeDifference(canvas);
    INFO(difference);
    CHECK(difference.empty());
}

}  // namespace

TEST_CASE("a frame arrives whole") {
    Terminal terminal{20, 3};
    FrameWriter writer;
    std::string bytes;

    Canvas canvas{20, 3};
    canvas.fill(1, 0, 20, Palette::panel());
    canvas.put(0, 0, "you     hello", Style{});
    canvas.put(1, 0, "npc 1   hello back", Palette::text().withBackground(Palette::panelColor()));
    canvas.put(2, 0, "\u65e5\u672c\u8a9e", Palette::accent());
    checkFrame(terminal, writer, canvas, bytes);
    CHECK_FALSE(bytes.empty());
}

TEST_CASE("a frame that looks the same costs nothing") {
    Terminal terminal{20, 3};
    FrameWriter writer;
    std::string bytes;

    Canvas canvas{20, 3};
    canvas.put(0, 0, "unchanged", Palette::text());
    canvas.put(1, 4, "also unchanged", Palette::muted());
    checkFrame(terminal, writer, canvas, bytes);

    const std::size_t firstSize = bytes.size();
    CHECK(firstSize > 0);
    checkFrame(terminal, writer, canvas, bytes);
    CHECK(bytes.empty());  // Nothing changed, so nothing is said.
}

TEST_CASE("only what changed is written, and it is a handful of bytes") {
    Terminal terminal{40, 4};
    FrameWriter writer;
    std::string bytes;

    Canvas canvas{40, 4};
    canvas.put(0, 0, "you     counting", Style{});
    canvas.put(1, 0, "npc 1   one", Palette::text());
    canvas.put(2, 0, "npc 1   two", Palette::text());
    checkFrame(terminal, writer, canvas, bytes);
    const std::size_t wholeFrame = bytes.size();

    // A word grows at the end of an answer: what is on the wire is that word, its style and the
    // move that gets to it, not the frame.
    canvas.put(2, 8, "two and three", Palette::text());
    checkFrame(terminal, writer, canvas, bytes);
    CHECK(bytes.size() < wholeFrame / 2);
}

TEST_CASE("a line that grows pushes the rows below and the tail is cleared when it shrinks") {
    Terminal terminal{24, 4};
    FrameWriter writer;
    std::string bytes;

    Canvas tall{24, 4};
    tall.put(0, 0, "first", Style{});
    tall.put(1, 0, "second", Style{});
    tall.put(2, 0, "third", Style{});
    tall.put(3, 0, "fourth", Style{});
    checkFrame(terminal, writer, tall, bytes);

    Canvas shortFrame{24, 4};
    shortFrame.put(0, 0, "first", Style{});
    shortFrame.put(1, 0, "second", Style{});
    checkFrame(terminal, writer, shortFrame, bytes);
    CHECK(terminal.at(2, 0).isBlank());
    CHECK(terminal.at(3, 0).isBlank());
    CHECK(terminal.at(3, 0).style.isDefault());
}

TEST_CASE("a panel that arrives is painted, background and all") {
    Terminal terminal{16, 2};
    FrameWriter writer;
    std::string bytes;

    Canvas plain{16, 2};
    plain.put(0, 0, "npc 1   hello", Palette::text());
    checkFrame(terminal, writer, plain, bytes);

    Canvas panel{16, 2};
    panel.fill(1, 0, 16, Palette::raised());
    panel.put(1, 2, "/create", Palette::accent().withBackground(Palette::raisedColor()));
    checkFrame(terminal, writer, panel, bytes);
    CHECK(terminal.at(1, 0).style.equals(Palette::raised()));
    CHECK(terminal.at(1, 2).style.equals(Palette::accent().withBackground(Palette::raisedColor())));
}

TEST_CASE("the same word in another colour is repainted without being rewritten elsewhere") {
    Terminal terminal{12, 1};
    FrameWriter writer;
    std::string bytes;

    Canvas canvas{12, 1};
    canvas.put(0, 0, "pattern", Palette::text());
    checkFrame(terminal, writer, canvas, bytes);

    canvas.put(0, 0, "pattern", Palette::accent());
    checkFrame(terminal, writer, canvas, bytes);
    CHECK(terminal.at(0, 0).style.equals(Palette::accent()));
    CHECK(terminal.at(0, 6).style.equals(Palette::accent()));
}

TEST_CASE("a resize repaints everything, at the new size") {
    Terminal terminal{12, 2};
    FrameWriter writer;
    std::string bytes;

    Canvas small{12, 2};
    small.put(0, 0, "small", Style{});
    checkFrame(terminal, writer, small, bytes);

    Terminal bigger{20, 4};
    FrameWriter other;
    Canvas large{20, 4};
    large.put(3, 15, "large", Palette::muted());
    checkFrame(bigger, other, large, bytes);

    // The same writer, given a frame of another size, paints all of it.
    Terminal resized{20, 4};
    checkFrame(resized, writer, large, bytes);
    CHECK(resized.matches(large));
}

TEST_CASE("the caret is left where it is asked for") {
    Terminal terminal{20, 3};
    FrameWriter writer;
    std::string bytes;

    Canvas canvas{20, 3};
    canvas.put(2, 0, "> ", Palette::panel());
    writer.setCaret(2, 4);
    bytes.clear();
    writer.emit(canvas, bytes);
    REQUIRE(terminal.apply(bytes));
    CHECK(terminal.matches(canvas));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
