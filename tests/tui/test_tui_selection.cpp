/// @file
/// @brief What the mouse picked out: which cells are inside it and what text they hold.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <string>

#include "screen/selection.h"
#include "screen/style.h"
#include "tui_test_support.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

namespace {

using namespace OpenAnimaTui::Tests;
using OpenAnimaTui::Screen::Clicks;
using OpenAnimaTui::Screen::lineAt;
using OpenAnimaTui::Screen::selectedText;
using OpenAnimaTui::Screen::Selection;
using OpenAnimaTui::Screen::wordAt;

/// A frame with three lines of text on it.
Canvas painted() {
    Canvas frame{20, 4};
    frame.put(0, 0, "you     hello", Palette::text());
    frame.put(1, 0, "npc 1   well met", Palette::primary());
    frame.put(2, 0, "  a note", Palette::muted());
    return frame;
}

}  // namespace

TEST_CASE("a selection picks out the cell the button went down on") {
    Selection selection;
    CHECK_FALSE(selection.active());
    CHECK(selection.empty());

    selection.begin(2, 4);
    CHECK(selection.active());
    CHECK_FALSE(selection.empty());
    CHECK(selection.contains(2, 4));
    CHECK_FALSE(selection.contains(2, 5));
    CHECK_FALSE(selection.contains(1, 4));
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

    // One cell is a selection: it is how a single character is taken.
    Selection oneCell;
    oneCell.begin(1, 10);
    CHECK(selectedText(frame, oneCell) == "l");
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

TEST_CASE("a double click takes the word under the pointer") {
    const Canvas frame = painted();

    // Row 1 reads "npc 1   well met".
    CHECK(selectedText(frame, wordAt(frame, 1, 10)) == "well");
    CHECK(selectedText(frame, wordAt(frame, 1, 14)) == "met");

    // A cell that is not part of a word is a word of its own.
    const Selection space = wordAt(frame, 1, 12);
    CHECK(space.contains(1, 12));
    CHECK_FALSE(space.contains(1, 11));

    // Off the frame there is nothing to take.
    CHECK(wordAt(frame, 9, 0).empty());
}

TEST_CASE("a word is letters, digits and underscores, and anything not ASCII") {
    Canvas frame{20, 4};
    frame.put(0, 0, "el ca\xc3\xb1on_2 del 2024", Palette::text());

    CHECK(selectedText(frame, wordAt(frame, 0, 4)) == "ca\xc3\xb1on_2");
    CHECK(selectedText(frame, wordAt(frame, 0, 17)) == "2024");
}

TEST_CASE("a triple click takes the whole row") {
    const Canvas frame = painted();
    CHECK(selectedText(frame, lineAt(frame, 1)) == "npc 1   well met");
    CHECK(lineAt(frame, 9).empty());
}

TEST_CASE("what the client draws for itself is not copied") {
    Canvas frame{12, 3};
    frame.put(0, 0, "\u250C\u2500\u2510", Palette::muted());
    frame.put(1, 0, "\u2502 \u2771 hi", Palette::text());
    frame.put(2, 0, "\u2514\u2500\u2518", Palette::muted());
    frame.put(1, 11, OpenAnimaTui::Screen::Glyphs::ScrollBar, Palette::muted());

    Selection all;
    all.begin(0, 0);
    all.extend(2, 11);

    // The frame, the prompt marker and the scrollbar stay out of it: a selection across them
    // comes out with the words and nothing else.
    CHECK(selectedText(frame, all) == "\n  hi\n");
}

TEST_CASE("presses in the same cell in a row are counted as clicks") {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start{};
    Clicks clicks;

    CHECK(clicks.press(2, 5, start) == 1);
    CHECK(clicks.press(2, 5, start + std::chrono::milliseconds{200}) == 2);
    CHECK(clicks.count() == 2);
    CHECK(clicks.press(2, 5, start + std::chrono::milliseconds{300}) == 3);

    // A slow press starts over, and so does one in another cell.
    CHECK(clicks.press(2, 5, start + std::chrono::milliseconds{2000}) == 1);
    CHECK(clicks.press(2, 6, start + std::chrono::milliseconds{2100}) == 1);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
