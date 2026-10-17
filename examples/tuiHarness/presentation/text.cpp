#include "text.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace OpenAnimaTui::Presentation {

namespace {

/// Ranges that take no column: combining marks, joiners and the invisible formatting ones.
struct Range {
    std::uint32_t first;
    std::uint32_t last;
};

/// Combining marks and invisible characters, by range.
constexpr std::array<Range, 32> ZeroWidth = {{
    {0x0300, 0x036F},   {0x0483, 0x0489},   {0x0591, 0x05BD}, {0x05BF, 0x05BF}, {0x05C1, 0x05C2},
    {0x05C4, 0x05C5},   {0x0610, 0x061A},   {0x064B, 0x065F}, {0x0670, 0x0670}, {0x06D6, 0x06DC},
    {0x06DF, 0x06E4},   {0x0730, 0x074A},   {0x07A6, 0x07B0}, {0x0900, 0x0902}, {0x093C, 0x093C},
    {0x0941, 0x0948},   {0x0951, 0x0957},   {0x0962, 0x0963}, {0x0E31, 0x0E31}, {0x0E34, 0x0E3A},
    {0x0E47, 0x0E4E},   {0x1AB0, 0x1AFF},   {0x1DC0, 0x1DFF}, {0x200B, 0x200F}, {0x202A, 0x202E},
    {0x2060, 0x2064},   {0x20D0, 0x20F0},   {0xFE00, 0xFE0F}, {0xFE20, 0xFE2F}, {0xFEFF, 0xFEFF},
    {0x1F3FB, 0x1F3FF}, {0xE0100, 0xE01EF},
}};

/// Ideographs, kana, hangul and the emoji, by range: two columns each.
constexpr std::array<Range, 18> Wide = {{
    {0x1100, 0x115F},
    {0x2329, 0x232A},
    {0x2E80, 0x303E},
    {0x3041, 0x33FF},
    {0x3400, 0x4DBF},
    {0x4E00, 0x9FFF},
    {0xA000, 0xA4CF},
    {0xAC00, 0xD7A3},
    {0xF900, 0xFAFF},
    {0xFE10, 0xFE19},
    {0xFE30, 0xFE6F},
    {0xFF00, 0xFF60},
    {0xFFE0, 0xFFE6},
    {0x1F300, 0x1F64F},
    {0x1F680, 0x1F6FF},
    {0x1F900, 0x1F9FF},
    {0x20000, 0x2FFFD},
    {0x30000, 0x3FFFD},
}};

/// @param ranges Table to search, ordered by code point.
/// @param codepoint Code point to look for.
/// @return Whether the code point falls in one of the ranges.
template <std::size_t Size>
bool inRanges(const std::array<Range, Size>& ranges, std::uint32_t codepoint) {
    std::size_t low = 0;
    std::size_t high = Size;
    while (low < high) {
        const std::size_t middle = (low + high) / 2;
        const Range& entry = *std::next(ranges.begin(), static_cast<std::ptrdiff_t>(middle));
        if (codepoint < entry.first) {
            high = middle;
        } else if (codepoint > entry.last) {
            low = middle + 1;
        } else {
            return true;
        }
    }
    return false;
}

/// The rows a text is broken into, and the word being held back so it is never split in half.
struct Wrapper {
    std::vector<std::string> rows;  ///< Finished rows.
    std::string row;                ///< Row being built.
    std::size_t rowColumns{0};      ///< Columns the row takes so far.
    std::string word;               ///< Word being built, not placed yet.
    std::size_t wordColumns{0};     ///< Columns the word takes.
    std::size_t width{1};           ///< Columns a row may take.

    /// Finishes the row: trailing spaces never reach the screen, because a row that ends in one
    /// looks the same as the row without it and would be repainted for nothing.
    void pushRow() {
        while (!row.empty() && row.back() == ' ') {
            row.pop_back();
        }
        rows.push_back(row);
        row.clear();
        rowColumns = 0;
    }

    /// Places the word that is being built, starting a row for it when it does not fit.
    void flushWord() {
        if (word.empty()) {
            return;
        }
        if (rowColumns > 0 && rowColumns + wordColumns > width) {
            pushRow();
        }
        row += word;
        rowColumns += wordColumns;
        word.clear();
        wordColumns = 0;
    }

    /// Ends the row, whether or not it is full.
    void endLine() {
        flushWord();
        pushRow();
    }

    /// Adds a space, which only goes in when there is room for it.
    /// @param piece The space, as it was written.
    void addSpace(std::string_view piece) {
        flushWord();
        if (rowColumns < width) {
            row += piece;
            ++rowColumns;
        }
    }

    /// Adds a character to the word being built.
    /// @param piece The character, as it was written.
    /// @param columns Columns it takes.
    void addCharacter(std::string_view piece, std::size_t columns) {
        if (wordColumns + columns > width) {
            // The word alone is wider than a row: it is cut, code point by code point.
            flushWord();
            pushRow();
        }
        word += piece;
        wordColumns += columns;
    }

    /// Adds a mark that takes no column of its own to the word being built.
    /// @param piece The mark, as it was written.
    void addCombining(std::string_view piece) {
        if (!word.empty()) {
            word += piece;
        }
    }
};

}  // namespace

std::size_t utf8Length(unsigned char byte) {
    if (byte < 0x80) {
        return 1;
    }
    if ((byte >> 5) == 0x6) {
        return 2;
    }
    if ((byte >> 4) == 0xE) {
        return 3;
    }
    if ((byte >> 3) == 0x1E) {
        return 4;
    }
    return 1;
}

std::uint32_t decodeCodepoint(std::string_view text, std::size_t& offset) {
    if (offset >= text.size()) {
        return 0;
    }
    const auto first = static_cast<unsigned char>(text[offset]);
    const std::size_t length = utf8Length(first);
    if (length == 1 || offset + length > text.size()) {
        offset += 1;
        return first < 0x80 ? first : 0xFFFD;
    }

    std::uint32_t codepoint = first & ((1U << (8U - static_cast<unsigned>(length) - 1U)) - 1U);
    for (std::size_t index = 1; index < length; ++index) {
        const auto next = static_cast<unsigned char>(text[offset + index]);
        if ((next & 0xC0) != 0x80) {
            offset += 1;
            return 0xFFFD;
        }
        codepoint = (codepoint << 6U) | (next & 0x3FU);
    }
    offset += length;
    return codepoint;
}

std::size_t glyphColumns(std::uint32_t codepoint) {
    if (codepoint == 0) {
        return 0;
    }
    if (inRanges(ZeroWidth, codepoint)) {
        return 0;
    }
    if (inRanges(Wide, codepoint)) {
        return 2;
    }
    return 1;
}

std::size_t measure(std::string_view text) {
    std::size_t columns = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        const std::uint32_t codepoint = decodeCodepoint(text, offset);
        if (codepoint == '\n') {
            continue;
        }
        columns += glyphColumns(codepoint);
    }
    return columns;
}

std::string clip(std::string_view text, std::size_t columns) {
    if (measure(text) <= columns) {
        return std::string{text};
    }
    constexpr std::string_view Ellipsis = "...";
    if (columns <= Ellipsis.size()) {
        return std::string{Ellipsis.substr(0, columns)};
    }

    std::string kept;
    std::size_t used = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        const std::size_t start = offset;
        const std::uint32_t codepoint = decodeCodepoint(text, offset);
        const std::size_t glyph = glyphColumns(codepoint);
        if (used + glyph + Ellipsis.size() > columns) {
            break;  // The next character would push the dots off the end.
        }
        kept.append(text.substr(start, offset - start));
        used += glyph;
    }
    kept.append(Ellipsis);
    return kept;
}

std::vector<std::string> wrap(std::string_view text, std::size_t width) {
    Wrapper wrapper;
    if (width > 0) {
        wrapper.width = width;
    }

    for (std::size_t offset = 0; offset < text.size();) {
        const std::size_t start = offset;
        const std::uint32_t codepoint = decodeCodepoint(text, offset);
        const std::string piece{text.substr(start, offset - start)};

        if (codepoint == '\n') {
            wrapper.endLine();
            continue;
        }
        if (codepoint == ' ') {
            wrapper.addSpace(piece);
            continue;
        }

        const std::size_t columns = glyphColumns(codepoint);
        if (columns == 0) {
            // A combining mark joins the character before it, so it never starts a row of its own.
            wrapper.addCombining(piece);
        } else if (columns <= wrapper.width) {
            wrapper.addCharacter(piece, columns);
        }
        // A character wider than the whole row cannot be shown, and is dropped rather than
        // allowed to push the row out of the screen.
    }

    wrapper.endLine();
    return wrapper.rows;
}

}  // namespace OpenAnimaTui::Presentation
