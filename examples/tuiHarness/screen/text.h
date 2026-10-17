/// @file
/// @brief Text as the terminal measures it: bytes, code points and columns.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace OpenAnimaTui::Screen {

/// Length in bytes of the UTF-8 sequence that starts with a byte.
/// @param byte First byte of the sequence.
/// @return Bytes in the sequence, at least one.
[[nodiscard]] std::size_t utf8Length(unsigned char byte);

/// Reads the code point at a position and moves past it.
/// @param text Text to read.
/// @param offset Position in bytes, moved to the next code point.
/// @return The code point, or a replacement character when the bytes are not UTF-8.
[[nodiscard]] std::uint32_t decodeCodepoint(std::string_view text, std::size_t& offset);

/// How many columns a code point takes on the screen.
///
/// Combining marks and joiners take none, the ideographs and the emoji take two, everything
/// else takes one. It follows the ranges wcwidth uses, which is what terminals follow too.
/// @param codepoint Code point to measure.
/// @return Zero, one or two columns.
[[nodiscard]] std::size_t glyphColumns(std::uint32_t codepoint);

/// Columns a text takes, line breaks excluded.
/// @param text Text to measure.
/// @return Columns, never negative.
[[nodiscard]] std::size_t measure(std::string_view text);

/// Cuts a text to the columns it has, marking the cut with an ellipsis.
///
/// A text that fits is returned as it is. One that does not ends in `...`, which takes the last
/// three of the columns: a line that is too long for the room it sits in is cut there instead of
/// running over the layout behind it. A wide character is never half printed.
/// @param text Text to cut.
/// @param columns Columns available.
/// @return The text, cut with an ellipsis when it did not fit.
[[nodiscard]] std::string clip(std::string_view text, std::size_t columns);

/// Breaks a text into rows that fit a width, at spaces when there is one.
///
/// A row never splits a code point and never counts a wide character as one column. A newline
/// in the text starts a row of its own.
/// @param text Text to break.
/// @param width Columns available per row.
/// @return The rows, at least one.
[[nodiscard]] std::vector<std::string> wrap(std::string_view text, std::size_t width);

}  // namespace OpenAnimaTui::Screen
