/// @file
/// @brief The palette and the glyphs, borrowed from the OpenCode dark theme.

#pragma once

#include <array>
#include <cstddef>

namespace OpenAnimaTui::Screen {

/// The OpenCode dark palette, taken from its theme.json, as 24-bit SGR.
constexpr const char* Accent = "\x1b[38;2;157;124;216m";   // #9d7cd8
constexpr const char* Primary = "\x1b[38;2;250;178;131m";  // #fab283
constexpr const char* Muted = "\x1b[38;2;128;128;128m";    // #808080

/// Panel backgrounds, so the prompt reads as a box floating over the conversation.
constexpr const char* Panel = "\x1b[48;2;20;20;20m";   // #141414
constexpr const char* Raised = "\x1b[48;2;30;30;30m";  // #1e1e1e

/// Ends a colour, and ends a foreground colour only, which keeps a panel background.
constexpr const char* Reset = "\x1b[0m";
constexpr const char* ForegroundReset = "\x1b[39m";

/// The prompt marker, as OpenCode draws its prompts, and the dots it spins while working.
constexpr const char* PromptMarker = "\u2771";
constexpr std::array<const char*, 10> SpinnerFrames = {
    "\u280B", "\u2819", "\u2839", "\u2838", "\u283C",
    "\u2834", "\u2826", "\u2827", "\u2807", "\u280F",
};

constexpr std::size_t PrefixWidth = 8;  ///< Width every speaker prefix is padded to.
constexpr std::size_t PromptWidth = 2;  ///< Width of the prompt marker, one glyph plus a space.
constexpr std::size_t MaxHints = 6;     ///< Commands shown while one is being typed.

}  // namespace OpenAnimaTui::Screen
