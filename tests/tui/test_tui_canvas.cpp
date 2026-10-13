/// @file
/// @brief The grid the client paints on: clipping, wide characters and combining marks.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>

#include "screen/canvas.h"
#include "screen/cell.h"
#include "screen/style.h"
#include "screen/text.h"
#include "tui_test_support.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

namespace {

using namespace OpenAnimaTui::Tests;

}  // namespace

TEST_CASE("a full canvas starts blank") {
    const Canvas canvas{4, 2};
    CHECK(canvas.width() == 4);
    CHECK(canvas.height() == 2);
    CHECK(readRow(canvas, 0) == "    ");
    CHECK(canvas.at(0, 0).isBlank());
}

TEST_CASE("text is written where it is asked and clipped at the edge") {
    Canvas canvas{6, 2};
    canvas.put(0, 1, "hello world", Style{});
    CHECK(readRow(canvas, 0) == " hello");
    CHECK(readRow(canvas, 1) == "      ");
}

TEST_CASE("a line break moves to the next row, at the column it started") {
    Canvas canvas{6, 3};
    canvas.put(0, 2, "ab\ncd", Style{});
    CHECK(readRow(canvas, 0) == "  ab  ");
    CHECK(readRow(canvas, 1) == "  cd  ");
}

TEST_CASE("a wide character takes two cells and is never split") {
    Canvas canvas{4, 1};
    canvas.put(0, 0, "\u65e5\u672c\u8a9e", Style{});  // Three ideographs, six columns of text.
    CHECK(readRow(canvas, 0) == "\u65e5\u672c");      // The third one does not fit.
    CHECK(canvas.at(0, 0).wide);
    CHECK(canvas.at(0, 1).kind == Cell::Kind::Continuation);

    Canvas narrow{1, 1};
    narrow.put(0, 0, "\u65e5", Style{});
    CHECK(narrow.at(0, 0).isBlank());
}

TEST_CASE("half of a wide character is never left behind") {
    Canvas canvas{4, 1};
    canvas.put(0, 0, "\u65e5", Style{});
    canvas.put(0, 1, "x", Style{});
    CHECK(canvas.at(0, 0).isBlank());
    CHECK(readRow(canvas, 0) == " x  ");

    Canvas other{4, 1};
    other.put(0, 0, "\u65e5", Style{});
    other.put(0, 0, "y", Style{});
    CHECK(other.at(0, 1).isBlank());
    CHECK(readRow(other, 0) == "y   ");
}

TEST_CASE("a combining mark joins the character before it") {
    Canvas canvas{4, 1};
    canvas.put(0, 0, "n\u0303a", Style{});  // n with a tilde over it, then an a.
    CHECK(canvas.at(0, 0).length == 3);
    CHECK(canvas.at(0, 1).length == 1);
    CHECK(canvas.width() == 4);
}

TEST_CASE("a fill lays a background down without writing characters") {
    Canvas canvas{5, 1};
    canvas.fill(0, 1, 3, Palette::panel());
    CHECK(canvas.at(0, 0).style.isDefault());
    CHECK(canvas.at(0, 1).style.equals(Palette::panel()));
    CHECK(canvas.at(0, 4).style.isDefault());
    CHECK(readRow(canvas, 0) == "     ");
}

TEST_CASE("two rows compare equal when they paint the same, background included") {
    Canvas one{3, 1};
    Canvas other{3, 1};
    CHECK(one.rowEquals(other, 0));

    one.fill(0, 0, 3, Palette::raised());
    CHECK_FALSE(one.rowEquals(other, 0));

    other.fill(0, 0, 3, Palette::raised());
    CHECK(one.rowEquals(other, 0));

    one.put(0, 0, "x", Palette::raised());
    CHECK_FALSE(one.rowEquals(other, 0));
}

TEST_CASE("resizing keeps what fits and drops the rest") {
    Canvas canvas{4, 2};
    canvas.put(0, 0, "abcd", Style{});
    canvas.put(1, 0, "efgh", Style{});

    canvas.resize(2, 3);
    CHECK(canvas.width() == 2);
    CHECK(canvas.height() == 3);
    CHECK(readRow(canvas, 0) == "ab");
    CHECK(readRow(canvas, 1) == "ef");
    CHECK(readRow(canvas, 2) == "  ");
}

TEST_CASE("measuring counts what the terminal shows") {
    CHECK(OpenAnimaTui::Screen::measure("abc") == 3);
    CHECK(OpenAnimaTui::Screen::measure("ca\u00f1on") == 5);    // The tilde is part of the letter.
    CHECK(OpenAnimaTui::Screen::measure("\u65e5\u672c") == 4);  // Two ideographs, four columns.
    CHECK(OpenAnimaTui::Screen::measure("n\u0303") == 1);       // A combining mark adds none.
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
