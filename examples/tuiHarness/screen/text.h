/// @file
/// @brief Text as it lands on a terminal: code points, columns and rows.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace OpenAnimaTui::Screen {

/// How many bytes a UTF-8 code point takes, from its leading byte.
/// @param byte The leading byte.
/// @return The length, in bytes.
std::size_t utf8Length(unsigned char byte);

/// Columns a row occupies, ignoring the colour sequences it carries.
/// @param text The row, which may hold SGR sequences.
/// @return The columns, counting each code point as one.
std::size_t visibleColumns(const std::string& text);

/// Pads a row to the width with trailing spaces, so a full redraw leaves no leftovers.
/// @param row The row, which may hold SGR sequences.
/// @param width Columns wanted.
/// @return The row, padded.
std::string padRow(const std::string& row, std::size_t width);

/// Wraps text at the given width, keeping whole words when they fit.
/// @param text Text to wrap. Newlines are honoured, and a character is never split.
/// @param width Columns available.
/// @return One string per physical row, without the newlines.
std::vector<std::string> wrap(const std::string& text, std::size_t width);

}  // namespace OpenAnimaTui::Screen
