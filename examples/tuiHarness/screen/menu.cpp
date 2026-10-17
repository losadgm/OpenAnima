#include "menu.h"

#include <algorithm>
#include <string_view>

namespace OpenAnimaTui::Screen {

namespace {

/// Columns an entry is kept away from the stroke by, on each side.
constexpr std::size_t Padding = 1;

}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
MenuPlacement placeMenu(std::size_t row, std::size_t column, std::size_t width,
                        std::size_t height) {
    std::size_t widest = 0;
    for (const char* entry : MenuEntries) {
        widest = std::max(widest, std::string_view{entry}.size());
    }

    MenuPlacement placement;
    placement.columns = widest + (2 * Padding) + 2;
    placement.rows = MenuEntries.size() + 2;
    const std::size_t lastRow = height > placement.rows ? height - placement.rows : 0;
    const std::size_t lastColumn = width > placement.columns ? width - placement.columns : 0;
    placement.row = std::min(row, lastRow);
    placement.column = std::min(column, lastColumn);
    return placement;
}

std::optional<std::size_t> menuEntryAt(const MenuPlacement& placement, std::size_t row,
                                       std::size_t column) {
    const bool insideRows = row > placement.row && row + 1 < placement.row + placement.rows;
    const bool insideColumns =
        column > placement.column && column + 1 < placement.column + placement.columns;
    if (!insideRows || !insideColumns) {
        return std::nullopt;
    }
    const std::size_t index = row - placement.row - 1;
    if (index >= MenuEntries.size()) {
        return std::nullopt;
    }
    return index;
}

}  // namespace OpenAnimaTui::Screen
