/// @file
/// @brief Text as it lands on a terminal: code points, columns and rows.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "screen/text.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("wrap breaks at spaces and honours newlines") {
    CHECK(OpenAnimaTui::Screen::wrap("one two three", 8) ==
          std::vector<std::string>{"one two", "three"});
    CHECK(OpenAnimaTui::Screen::wrap("one\ntwo", 10) == std::vector<std::string>{"one", "two"});
    CHECK(OpenAnimaTui::Screen::wrap("a verylongword", 5) ==
          std::vector<std::string>{"a", "veryl", "ongwo", "rd"});
}

TEST_CASE("wrap does not split a multi-byte character") {
    CHECK(OpenAnimaTui::Screen::wrap("cañón", 3) == std::vector<std::string>{"cañ", "ón"});
}

TEST_CASE("visibleColumns counts code points and ignores colour") {
    CHECK(OpenAnimaTui::Screen::visibleColumns("abc") == 3);
    CHECK(OpenAnimaTui::Screen::visibleColumns("\x1b[38;2;1;2;3mabc\x1b[39m") == 3);
    CHECK(OpenAnimaTui::Screen::visibleColumns("cañón") == 5);
    CHECK(OpenAnimaTui::Screen::visibleColumns("\u2771 x") == 3);
}

TEST_CASE("padRow pads to the width and cuts what does not fit") {
    const std::string padded = OpenAnimaTui::Screen::padRow("\x1b[31mcañ\x1b[0m", 6);
    CHECK(OpenAnimaTui::Screen::visibleColumns(padded) == 6);
    CHECK(OpenAnimaTui::Screen::padRow("abcdef", 3) == "abc");

    // Cutting keeps whole characters and the colour that was open.
    const std::string cut = OpenAnimaTui::Screen::padRow("\x1b[31mcañón\x1b[0m", 4);
    CHECK(OpenAnimaTui::Screen::visibleColumns(cut) == 4);
    CHECK(cut.rfind("\x1b[31m", 0) == 0);
    CHECK(cut.find("ñ") != std::string::npos);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
