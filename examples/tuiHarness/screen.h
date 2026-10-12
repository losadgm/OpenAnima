/// @file
/// @brief The logic of the screen, with no terminal I/O of its own.
///
/// Everything here takes a width and strings and returns lines, so it can be tested with a
/// width instead of a terminal. The only terminal-shaped text it emits is colour, as ANSI
/// SGR sequences, which a test strips and a terminal honours.

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "terminal.h"

namespace OpenAnimaTui {

/// Who speaks a line. The colour of a role is decided here.
enum class Role : std::uint8_t { User, Npc };

/// The live region: the last lines of the terminal while a turn runs.
struct ScreenState {
    std::string answer;     ///< Text of the turn in flight.
    std::string draft;      ///< What the user is typing.
    std::size_t cursor{0};  ///< Draft cursor, in characters.
    std::string model;      ///< Model that answers.
    std::string hint;       ///< What the quiet line says when nothing is running.
    bool busy{false};       ///< A turn is in flight.
    bool cancelled{false};  ///< The turn in flight was cancelled.
    std::chrono::steady_clock::time_point startedAt;  ///< When the turn started.
    std::size_t liveLines{0};                         ///< Lines of the frame currently on screen.
};

/// Wraps text at the given width, keeping whole words when they fit.
/// @param text Text to wrap. Newlines are honoured.
/// @param width Columns available.
/// @return One string per physical row, without the newlines.
std::vector<std::string> wrap(const std::string& text, std::size_t width);

/// Renders one committed line: the speaker prefix and the wrapped body.
/// @param speaker Name that goes in the prefix.
/// @param text What was said.
/// @param width Columns available.
/// @param role Whose line it is.
/// @return One string per physical row, the first with the prefix, the rest indented.
std::vector<std::string> renderLine(const std::string& speaker, const std::string& text,
                                    std::size_t width, Role role);

/// Renders the live frame: the answer and the working line while a turn runs, or the
/// quiet line, and the prompt with the draft under it.
/// @param state What to draw.
/// @param width Columns available.
/// @return The frame, rows separated by \n and no trailing newline.
std::string renderFrame(const ScreenState& state, std::size_t width);

/// Where the draft cursor sits, measured from the position right after the last
/// character of the frame, which is where the terminal cursor is left after the frame is
/// written.
/// @param state What is drawn.
/// @param width Columns available.
/// @return How many rows up and columns left the cursor has to move to reach the caret.
std::pair<std::size_t, std::size_t> caretOffset(const ScreenState& state, std::size_t width);

/// What a key means for the loop.
enum class Action : std::uint8_t {
    None,
    Insert,           ///< Add the key's character at the cursor.
    Backspace,        ///< Delete the character before the cursor.
    MoveLeft,         ///< One character left.
    MoveRight,        ///< One character right.
    MoveHome,         ///< Start of the draft.
    MoveEnd,          ///< End of the draft.
    Submit,           ///< Send the draft.
    Newline,          ///< Insert a newline instead of sending.
    Cancel,           ///< Cancel the turn in flight.
    ClearDraft,       ///< Empty the draft.
    Leave,            ///< End the session.
    HistoryPrevious,  ///< Walk the prompts backwards.
    HistoryNext,      ///< Walk the prompts forwards.
};

/// Maps a key to the action it triggers.
/// @param key The key.
/// @param busy Whether a turn is in flight.
/// @param draftEmpty Whether the draft holds no text.
/// @return The action. Inserting is the caller's job, with the key's character.
Action mapKey(const Key& key, bool busy, bool draftEmpty);

/// How long a turn has been running, in tenths of a second.
/// @param state The live region.
/// @return Tenths of a second, or 0 when nothing is running.
std::size_t elapsedTenths(const ScreenState& state);

}  // namespace OpenAnimaTui
