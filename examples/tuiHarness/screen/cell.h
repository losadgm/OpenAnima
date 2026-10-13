/// @file
/// @brief One cell of the screen: what is shown in it and how it is painted.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "style.h"

namespace OpenAnimaTui::Screen {

/// One cell of the screen.
///
/// A cell holds a code point, its combining marks, and the style it is painted with. A wide
/// character (an ideograph, an emoji) owns the cell to its right, which is marked as a
/// continuation: nothing is written in it, and the pair is only ever replaced as a pair.
struct Cell {
    /// What the cell holds.
    enum class Kind : std::uint8_t {
        Blank,         ///< Nothing written: a space with the style of the cell.
        Glyph,         ///< A character, wide or not.
        Continuation,  ///< The right half of a wide character.
    };

    static constexpr std::size_t MaxGlyphBytes = 12;  ///< A code point and its combining marks.

    Kind kind{Kind::Blank};                   ///< What the cell holds.
    std::array<char, MaxGlyphBytes> glyph{};  ///< UTF-8 bytes of the character.
    std::uint8_t length{0};                   ///< Bytes used in `glyph`.
    bool wide{false};                         ///< Whether the character covers the next cell too.
    Style style{};                            ///< How the cell is painted.

    /// @return Whether the cell holds nothing to show.
    [[nodiscard]] bool isBlank() const;

    /// Appends a code point that takes no column of its own to the character in the cell.
    /// @param bytes UTF-8 bytes of the code point.
    /// @return Whether it fitted.
    bool appendCombining(std::string_view bytes);

    /// @param other Cell to compare with.
    /// @return Whether both would paint the same thing.
    [[nodiscard]] bool equals(const Cell& other) const;
};

}  // namespace OpenAnimaTui::Screen
