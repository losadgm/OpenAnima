/// @file
/// @brief The menu the right button opens, which is where copying and pasting live.

#pragma once

#include <array>
#include <cstddef>
#include <optional>

namespace OpenAnimaTui::Screen {

/// The entries the menu offers, in the order they are drawn.
inline constexpr std::array<const char*, 2> MenuEntries = {"copy", "paste"};

/// Where the box of the menu lands on a screen: a row for each entry and one for each stroke.
struct MenuPlacement {
    std::size_t row{0};      ///< First row of the box.
    std::size_t column{0};   ///< First column of the box.
    std::size_t rows{0};     ///< Rows the box takes.
    std::size_t columns{0};  ///< Columns the box takes.
};

/// Places the menu next to where the pointer was, kept inside the screen.
/// @param row Row the menu would start at.
/// @param column Column the menu would start at.
/// @param width Columns on the screen.
/// @param height Rows on the screen.
/// @return Where the box goes.
// A position and then the size of the screen: counts, all named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] MenuPlacement placeMenu(std::size_t row, std::size_t column, std::size_t width,
                                      std::size_t height);

/// @param placement Where the menu is.
/// @param row Row the pointer is at.
/// @param column Column the pointer is at.
/// @return The entry under the pointer, or nothing when the pointer is not on one.
// As everywhere here: a row and a column, both named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
[[nodiscard]] std::optional<std::size_t> menuEntryAt(const MenuPlacement& placement,
                                                     std::size_t row, std::size_t column);

/// The menu the client shows: whether it is up, where, and which entry is picked out.
struct Menu {
    bool open{false};           ///< Whether the menu is up.
    MenuPlacement placement{};  ///< Where the box is.
    std::size_t selected{0};    ///< Entry that is picked out.
};

}  // namespace OpenAnimaTui::Screen
