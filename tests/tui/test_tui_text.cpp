/// @file
/// @brief Text as the terminal measures it: columns, wrapping and code points.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "screen/text.h"
#include "tui_test_support.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

namespace {

using OpenAnimaTui::Screen::glyphColumns;
using OpenAnimaTui::Screen::measure;
using OpenAnimaTui::Screen::utf8Length;
using OpenAnimaTui::Screen::wrap;

}  // namespace

TEST_CASE("a code point is measured in the columns it takes") {
    CHECK(glyphColumns('a') == 1);
    CHECK(glyphColumns(' ') == 1);
    CHECK(glyphColumns(0x00F1) == 1);   // n with a tilde: a letter of its own.
    CHECK(glyphColumns(0x0303) == 0);   // A tilde on its own, which overlays the letter.
    CHECK(glyphColumns(0x65E5) == 2);   // An ideograph.
    CHECK(glyphColumns(0x1F600) == 2);  // An emoji.
    CHECK(glyphColumns(0x200B) == 0);   // A zero width space.
}

TEST_CASE("measuring a text counts what is shown, not what is stored") {
    CHECK(measure("abc") == 3);
    CHECK(measure("ca\u00f1on") == 5);
    CHECK(measure("\u65e5\u672c") == 4);
    CHECK(measure("n\u0303") == 1);
    CHECK(measure("") == 0);
}

TEST_CASE("the bytes of a sequence are known from its first byte") {
    CHECK(utf8Length('a') == 1);
    CHECK(utf8Length(0xC3) == 2);
    CHECK(utf8Length(0xE6) == 3);
    CHECK(utf8Length(0xF0) == 4);
    CHECK(utf8Length(0x80) == 1);  // A stray continuation byte is a character of its own.
}

TEST_CASE("wrapping breaks at spaces and honours line breaks") {
    CHECK(wrap("one two three", 8) == std::vector<std::string>{"one two", "three"});
    CHECK(wrap("one\ntwo", 10) == std::vector<std::string>{"one", "two"});
    CHECK(wrap("a verylongword", 5) == std::vector<std::string>{"a", "veryl", "ongwo", "rd"});
}

TEST_CASE("wrapping never splits a character and counts the wide ones as two") {
    CHECK(wrap("ca\u00f1\u00f3n", 3) == std::vector<std::string>{"ca\u00f1", "\u00f3n"});
    CHECK(wrap("\u65e5\u672c\u8a9e", 4) == std::vector<std::string>{"\u65e5\u672c", "\u8a9e"});
    // A character wider than the whole row is dropped rather than shown cut in half.
    CHECK(wrap("\u65e5", 1) == std::vector<std::string>{""});
}

TEST_CASE("every row of a wrapped text fits the width it was given") {
    const std::string text =
        "The smith looks up from the anvil and says something about iron and patience";
    for (std::size_t width = 1; width < 40; ++width) {
        for (const std::string& row : wrap(text, width)) {
            CHECK(measure(row) <= width);
        }
    }
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
