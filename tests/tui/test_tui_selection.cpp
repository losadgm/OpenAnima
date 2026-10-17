/// @file
/// @brief What the mouse picked out: which cells are inside it and what text they hold.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>

#include "screen/selection.h"
#include "screen/style.h"
#include "tui_test_support.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

namespace {

using namespace OpenAnimaTui::Tests;
using OpenAnimaTui::Screen::Bounds;
using OpenAnimaTui::Screen::boundsOf;
using OpenAnimaTui::Screen::Clicks;
using OpenAnimaTui::Screen::lineAt;
using OpenAnimaTui::Screen::Scope;
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

/// @param frame Frame that was painted.
/// @return Every row of it, which is as far as a span of the whole screen reaches.
Bounds allRows(const Canvas& frame) {
    return Bounds{0, frame.height()};
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
    CHECK(selectedText(frame, oneRow, allRows(frame)) == "you     hell");

    Selection severalRows;
    severalRows.begin(0, 8);
    severalRows.extend(1, 11);
    CHECK(selectedText(frame, severalRows, allRows(frame)) == "hello\nnpc 1   well");

    Selection wholeRows;
    wholeRows.begin(0, 0);
    wholeRows.extend(2, 19);
    CHECK(selectedText(frame, wholeRows, allRows(frame)) ==
          "you     hello\nnpc 1   well met\n  a note");

    // One cell is a selection: it is how a single character is taken.
    Selection oneCell;
    oneCell.begin(1, 10);
    CHECK(selectedText(frame, oneCell, allRows(frame)) == "l");
}

TEST_CASE("blank layout around the text is not part of what was selected") {
    const Canvas frame = painted();
    Selection selection;
    selection.begin(0, 13);
    selection.extend(1, 19);
    CHECK(selectedText(frame, selection, allRows(frame)) == "npc 1   well met");
}

TEST_CASE("scrolling takes the selection with the words it covers") {
    Selection selection;
    selection.begin(1, 4);
    selection.extend(2, 6);

    // The text moves down a row, so the span does too.
    selection.shift(1);
    CHECK(selection.firstRow() == 2);
    CHECK(selection.lastRow() == 3);
    CHECK(selection.contains(2, 4));
    CHECK(selection.contains(3, 6));
    CHECK_FALSE(selection.contains(1, 4));

    // And back up when the text goes the other way.
    selection.shift(-1);
    CHECK(selection.firstRow() == 1);
    CHECK(selection.lastRow() == 2);
}

TEST_CASE("a selection that scrolls out of view is there when the text comes back") {
    Selection selection;
    selection.begin(1, 0);
    selection.extend(3, 4);

    // Scrolled far enough that the whole span is below the viewport.
    selection.shift(40);
    CHECK(selection.active());
    CHECK(selection.firstRow() == 41);
    CHECK(selection.lastRow() == 43);
    CHECK_FALSE(selection.contains(0, 0));

    // And far the other way, above the top, without losing it either.
    selection.shift(-80);
    CHECK(selection.active());
    CHECK(selection.firstRow() == -39);
    CHECK(selection.lastRow() == -37);

    // Back where it was made.
    selection.shift(40);
    CHECK(selection.firstRow() == 1);
    CHECK(selection.lastRow() == 3);
    CHECK(selection.contains(2, 0));
}

TEST_CASE("a selection made outside the conversation does not move with the text") {
    Selection selection;
    selection.begin(2, 0, Scope::Screen);
    selection.extend(2, 4);

    selection.shift(40);
    CHECK(selection.firstRow() == 2);
    CHECK(selection.lastRow() == 2);

    selection.shift(-400);
    CHECK(selection.firstRow() == 2);
    CHECK(selection.lastRow() == 2);
}

TEST_CASE("what is read stops at the rows of the scope") {
    const Canvas frame = painted();
    Selection selection;
    selection.begin(1, 0);
    selection.extend(3, 19);

    CHECK(selectedText(frame, selection, Bounds{0, 3}) == "npc 1   well met\n  a note");
    CHECK(selectedText(frame, selection, Bounds{0, 2}) == "npc 1   well met");
    CHECK(selectedText(frame, selection, Bounds{0, 0}).empty());

    // A span pushed above the top reads from the first row that is still on the screen.
    selection.shift(-3);
    CHECK(selectedText(frame, selection, Bounds{0, 4}) == "you     hello");
}

TEST_CASE("each scope takes its own rows of the screen") {
    CHECK(boundsOf(Scope::Conversation, 6, 10).first == 0);
    CHECK(boundsOf(Scope::Conversation, 6, 10).count == 6);
    CHECK(boundsOf(Scope::Screen, 6, 10).first == 6);
    CHECK(boundsOf(Scope::Screen, 6, 10).count == 4);
    CHECK(boundsOf(Scope::Screen, 6, 10).end() == 10);

    // A screen with no room for the conversation leaves nothing of it, and the prompt keeps
    // whatever is left.
    CHECK(boundsOf(Scope::Conversation, 0, 10).empty());
    CHECK(boundsOf(Scope::Screen, 0, 10).count == 10);
    CHECK(boundsOf(Scope::Screen, 80, 10).empty());
}

TEST_CASE("a span of the prompt does not read what is said above it") {
    const Canvas frame = painted();
    Selection selection;
    selection.begin(1, 0, Scope::Screen);
    selection.extend(3, 19);

    // The conversation takes the first two rows and the rest of the screen is below them, so
    // a drag that came up from the prompt reads only what is under it.
    CHECK(selectedText(frame, selection, Bounds{2, 2}) == "  a note");
    CHECK(selectedText(frame, selection, Bounds{4, 0}).empty());
}

TEST_CASE("scrolling leaves a selection that is not there alone") {
    Selection selection;
    selection.shift(2);
    CHECK_FALSE(selection.active());
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
    CHECK(selectedText(frame, wordAt(frame, 1, 10), allRows(frame)) == "well");
    CHECK(selectedText(frame, wordAt(frame, 1, 14), allRows(frame)) == "met");

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

    CHECK(selectedText(frame, wordAt(frame, 0, 4), allRows(frame)) == "ca\xc3\xb1on_2");
    CHECK(selectedText(frame, wordAt(frame, 0, 17), allRows(frame)) == "2024");
}

TEST_CASE("a triple click takes the whole row") {
    const Canvas frame = painted();
    CHECK(selectedText(frame, lineAt(frame, 1), allRows(frame)) == "npc 1   well met");
    CHECK(lineAt(frame, 9).empty());
}

TEST_CASE("what the client draws for itself is not copied") {
    Canvas frame{12, 3};
    const Style frameStyle = Palette::muted().withBackground(Palette::panelColor());
    const Style textStyle = Palette::text().withBackground(Palette::panelColor());
    frame.put(0, 0, "\U0001FB7D\u2594\u2594", frameStyle);
    frame.put(1, 0, "\u258F", frameStyle);
    frame.put(1, 2, "\u2771", textStyle);
    frame.put(1, 4, "hi", textStyle);
    frame.put(2, 0, "\U0001FB7C\u2581\u2581", frameStyle);
    frame.put(1, 11, OpenAnimaTui::Screen::Glyphs::ScrollBar, Palette::muted());

    Selection all;
    all.begin(0, 0);
    all.extend(2, 11);

    // The frame, the prompt marker, the scrollbar and the blank layout stay out of it: a
    // selection across them comes out with the words and nothing else.
    CHECK(selectedText(frame, all, allRows(frame)) == "hi");
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
