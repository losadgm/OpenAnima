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
        CtrlEnter,   ///< Control and Return together: a new line, never a send.
        Backspace,   ///< Delete the character before the cursor.
        Tab,         ///< The tab key.
        Escape,      ///< Escape.
        ArrowUp,     ///< Cursor up.
        ArrowDown,   ///< Cursor down.
        ArrowLeft,   ///< Cursor left.
        ArrowRight,  ///< Cursor right.
        PageUp,      ///< One page of transcript up.
        PageDown,    ///< One page of transcript down.
        WheelUp,     ///< The wheel, one notch towards the beginning of the conversation.
        WheelDown,   ///< The wheel, one notch towards the end.
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

    /// @return The height of the terminal in rows, 24 when it cannot be asked.
    [[nodiscard]] virtual std::size_t height() const = 0;

    /// Waits for a key.
    /// @param timeout Longest wait before giving up.
    /// @return The key, or nothing when the timeout passed.
    virtual std::optional<Key> readKey(std::chrono::milliseconds timeout) = 0;

    /// Hands text to the terminal.
    ///
    /// What was written may not have left the process when this returns: a terminal that is not
    /// reading fast enough would otherwise block the client, and a client blocked on writing is a
    /// client that has stopped reading the keyboard. What is left over is kept and sent by
    /// `flush`, and `hasPending` says whether there is any.
    /// @param text Text to write.
    virtual void write(std::string_view text) = 0;

    /// Tries to send what is still waiting for the terminal.
    /// @return Whether everything written so far has left the process.
    virtual bool flush() = 0;

    /// @return Whether something written has not left the process yet.
    [[nodiscard]] virtual bool hasPending() const = 0;

    /// Switches to the alternate screen, where the client owns every row, and asks the terminal
    /// for the keyboard and the mouse it can report: the enhanced keyboard where it exists, one
    /// event per paste, and the wheel as a wheel instead of the arrow keys the terminal would
    /// otherwise make of it. The caret is left where the client puts it.
    ///
    /// Asking for the mouse means the terminal stops handling the wheel and the selection
    /// itself: it is the price of the wheel scrolling the conversation rather than the
    /// terminal's own scrollback, which has nothing in it while the alternate screen is up.
    /// Holding shift still selects text in most terminals.
    void enterAlternateScreen() {
        write("\x1b[?1049h\x1b[>1u\x1b[?2004h\x1b[?1000h\x1b[?1006h");
    }

    /// Leaves the alternate screen and hands the terminal back to the shell, with the keyboard
    /// and the mouse reported the way they were.
    void leaveAlternateScreen() {
        write("\x1b[<u\x1b[?2004l\x1b[?1006l\x1b[?1000l\x1b[?1049l");
    }
};

/// Makes the terminal for this platform, in raw mode.
/// @throws std::runtime_error when standard input is not a terminal.
std::unique_ptr<Terminal> makeTerminal();

}  // namespace OpenAnimaTui
