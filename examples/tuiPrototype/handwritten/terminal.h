/// @file
/// @brief Terminal handling for the hand written prototype: raw mode, keys and drawing.
///
/// Throwaway code: it exists to compare this approach against FTXUI, and only the Unix
/// backend is written. The Windows one is what the comparison has to price in.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace OpenAnimaPrototype {

/// What the keyboard produced.
struct Key {
    enum class Kind : std::uint8_t {
        None,       ///< Nothing was pressed before the timeout.
        Character,  ///< A printable character, in `character`.
        Enter,      ///< The return key.
        Backspace,  ///< The backspace key.
        Escape,     ///< The escape key.
        CtrlC,      ///< Interrupt.
        CtrlD,      ///< End of input.
        Unknown     ///< A key this prototype does not care about.
    };

    Kind kind{Kind::None};  ///< What happened.
    char character{0};      ///< The character, for Kind::Character.
};

/// Puts the terminal in raw mode and puts it back when it goes out of scope.
class RawMode {
public:
    /// @throws std::runtime_error when the terminal cannot be configured.
    RawMode();
    ~RawMode();

    RawMode(const RawMode&) = delete;
    RawMode& operator=(const RawMode&) = delete;
    RawMode(RawMode&&) = delete;
    RawMode& operator=(RawMode&&) = delete;

private:
    bool m_active{false};
};

/// @return The width of the terminal in columns, 80 when it cannot be asked.
std::size_t terminalWidth();

/// Waits for a key.
/// @param timeout Longest wait before giving up.
/// @return The key, or nothing when the timeout passed.
std::optional<Key> readKey(std::chrono::milliseconds timeout);

/// Wraps text at the given width, keeping whole words when they fit.
/// @param text Text to wrap. Newlines are honoured.
/// @param width Columns available.
/// @return The lines, without the newlines.
std::vector<std::string> wrap(const std::string& text, std::size_t width);

/// Writes text to standard output.
/// @param text Text to write.
void write(const std::string& text);

}  // namespace OpenAnimaPrototype
