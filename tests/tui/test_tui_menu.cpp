/// @file
/// @brief The menu the right button opens: where it goes and which entry is under the pointer.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>

#include "screen/menu.h"

using OpenAnimaTui::Screen::MenuEntries;
using OpenAnimaTui::Screen::menuEntryAt;
using OpenAnimaTui::Screen::MenuPlacement;
using OpenAnimaTui::Screen::placeMenu;

TEST_CASE("the menu goes where the pointer is when it fits") {
    const MenuPlacement placement = placeMenu(2, 4, 40, 20);
    CHECK(placement.row == 2);
    CHECK(placement.column == 4);
    CHECK(placement.rows == MenuEntries.size() + 2);
    CHECK(placement.columns > 5);  // the longest entry and the padding around it
}

TEST_CASE("the menu is kept inside the screen") {
    const MenuPlacement placement = placeMenu(19, 39, 40, 20);
    CHECK(placement.row + placement.rows <= 20);
    CHECK(placement.column + placement.columns <= 40);
}

TEST_CASE("a pointer on an entry says which one, and anywhere else says nothing") {
    const MenuPlacement placement = placeMenu(0, 0, 40, 20);

    // The first row, the first column and the last one of each are the strokes of the box.
    CHECK_FALSE(menuEntryAt(placement, placement.row, placement.column + 2).has_value());
    CHECK_FALSE(menuEntryAt(placement, placement.row + 1, placement.column).has_value());
    CHECK_FALSE(menuEntryAt(placement, placement.row + 1, placement.column + placement.columns - 1)
                    .has_value());
    CHECK(menuEntryAt(placement, placement.row + 1, placement.column + 2) == 0);
    CHECK(menuEntryAt(placement, placement.row + 2, placement.column + 2) == 1);
    CHECK_FALSE(menuEntryAt(placement, placement.row + placement.rows - 1, placement.column + 2)
                    .has_value());
    CHECK_FALSE(
        menuEntryAt(placement, placement.row + placement.rows, placement.column + 2).has_value());
}
