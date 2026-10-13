/// @file
/// @brief What the mouse picked out: which cells are inside it and what text they hold.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>

#include "screen/selection.h"
#include "screen/style.h"
#include "tui_test_support.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

namespace {

using namespace OpenAnimaTui::Tests;
using OpenAnimaTui::Screen::selectedText;
using OpenAnimaTui::Screen::Selection;

/// A frame with three lines of text on it.
Canvas painted() {
    Canvas frame{20, 4};
    frame.put(0, 0, "you     hello", Palette::text());
    frame.put(1, 0, "npc 1   well met", Palette::primary());
    frame.put(2, 0, "  a note", Palette::muted());
    return frame;
}

}  // namespace

TEST_CASE("a selection that covers nothing is not a selection") {
    Selection selection;
    CHECK_FALSE(selection.active());
    CHECK(selection.empty());

    selection.begin(2, 4);
    CHECK(selection.active());
    CHECK(selection.empty());

    selection.extend(2, 9);
    CHECK_FALSE(selection.empty());
}

TEST_CASE("a selection runs from one position to the other, whichever way it was made") {
    Selection selection;
    selection.begin(1, 4);
    selection.extend(2, 3);

    CHECK_FALSE(selection.contains(1, 3));
    CHECK(selection.contains(1, 4));
    CHECK(selection.contains(1, 19));
    CHECK(selection.contains(2, 0));
    CHECK(selection.contains(2, 3));
    CHECK_FALSE(selection.contains(2, 4));
    CHECK_FALSE(selection.contains(0, 0));
    CHECK(selection.firstRow() == 1);
    CHECK(selection.lastRow() == 2);
}

TEST_CASE("a selection made upwards covers the same cells as one made downwards") {
    Selection down;
    down.begin(0, 3);
    down.extend(2, 2);

    Selection upper;
    upper.begin(2, 2);
    upper.extend(0, 3);

    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 20; ++column) {
            CHECK(down.contains(row, column) == upper.contains(row, column));
        }
    }
}

TEST_CASE("the text of a selection is what the frame shows there") {
    const Canvas frame = painted();

    Selection oneRow;
    oneRow.begin(0, 0);
    oneRow.extend(0, 11);
    CHECK(selectedText(frame, oneRow) == "you     hell");

    Selection severalRows;
    severalRows.begin(0, 8);
    severalRows.extend(1, 11);
    CHECK(selectedText(frame, severalRows) == "hello\nnpc 1   well");

    Selection wholeRows;
    wholeRows.begin(0, 0);
    wholeRows.extend(2, 19);
    CHECK(selectedText(frame, wholeRows) == "you     hello\nnpc 1   well met\n  a note");

    Selection click;
    click.begin(1, 5);
    CHECK(selectedText(frame, click).empty());
}

TEST_CASE("a trailing blank is not part of what was selected") {
    const Canvas frame = painted();
    Selection selection;
    selection.begin(0, 13);
    selection.extend(1, 19);
    CHECK(selectedText(frame, selection) == "\nnpc 1   well met");
}

TEST_CASE("clearing a selection leaves nothing picked out") {
    Selection selection;
    selection.begin(0, 0);
    selection.extend(2, 5);
    selection.clear();
    CHECK_FALSE(selection.active());
    CHECK_FALSE(selection.contains(1, 1));
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
