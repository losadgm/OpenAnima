/// @file
/// @brief The terminal behind the TUI client: raw mode, keys and writing.
///
/// The client never touches the console on its own: it reads keys from this layer and
/// writes text to it. One implementation is Unix (termios), one is Windows (the console
/// API with virtual terminal sequences), and both satisfy the same interface.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace OpenAnimaTui {

/// What the keyboard produced.
struct Key {
    enum class Kind : std::uint8_t {
        None,        ///< Nothing was pressed before the timeout.
        Character,   ///< A printable character, in `character`.
        Enter,       ///< Return.
        Backspace,   ///< Delete the character before the cursor.
        Tab,         ///< The tab key.
        Escape,      ///< Escape.
        ArrowUp,     ///< Cursor up.
        ArrowDown,   ///< Cursor down.
        ArrowLeft,   ///< Cursor left.
        ArrowRight,  ///< Cursor right.
        CtrlC,       ///< Interrupt.
        CtrlD,       ///< End of input.
        CtrlJ,       ///< Newline without sending.
        CtrlA,       ///< Start of line.
        CtrlB,       ///< One character left.
        CtrlE,       ///< End of line.
        CtrlF,       ///< One character right.
        CtrlN,       ///< Next history item.
        CtrlP,       ///< Previous history item.
        Unknown      ///< A key this client does not use.
    };

    Kind kind{Kind::None};  ///< What happened.
    char character{0};      ///< The character, for Kind::Character.
};

/// One terminal, put in raw mode for as long as it lives.
class Terminal {
public:
    Terminal() = default;
    virtual ~Terminal() = default;

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&) = delete;
    Terminal& operator=(Terminal&&) = delete;

    /// @return The width of the terminal in columns, 80 when it cannot be asked.
    [[nodiscard]] virtual std::size_t width() const = 0;

    /// Waits for a key.
    /// @param timeout Longest wait before giving up.
    /// @return The key, or nothing when the timeout passed.
    virtual std::optional<Key> readKey(std::chrono::milliseconds timeout) = 0;

    /// Writes text to the terminal.
    /// @param text Text to write.
    virtual void write(std::string_view text) = 0;
};

/// Makes the terminal for this platform, in raw mode.
/// @throws std::runtime_error when standard input is not a terminal.
std::unique_ptr<Terminal> makeTerminal();

}  // namespace OpenAnimaTui
