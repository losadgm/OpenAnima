/// @file
/// @brief The logic of the screen, with no terminal I/O of its own.
///
/// Everything here takes a width, a height and strings and returns rows, so it can be tested
/// without a terminal. The only terminal-shaped text it emits is colour, as ANSI SGR
/// sequences, which a test strips and a terminal honours.

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "terminal.h"

namespace OpenAnimaTui {

/// Who speaks a line. The colour of a role is decided here.
enum class Role : std::uint8_t { User, Npc, System };

/// One finished turn of the conversation, drawn oldest first.
struct Message {
    Role role;
    std::string text;
};

/// What the client is showing: the conversation, the prompt and where the view sits.
struct ScreenState {
    std::vector<Message> messages;  ///< Finished turns, oldest first.
    std::string answer;             ///< Text of the turn in flight.
    std::string draft;              ///< What the user is typing.
    std::size_t cursor{0};          ///< Draft cursor, in characters.
    std::string model;              ///< Model that answers.
    std::string hint;               ///< What the quiet line says when nothing is running.
    std::string npcLabel;           ///< How the active NPC is shown, empty when there is none.
    bool busy{false};               ///< A turn is in flight.
    bool cancelled{false};          ///< The turn in flight was cancelled.
    std::chrono::steady_clock::time_point startedAt;  ///< When the turn started.
    std::size_t scrollOffset{0};  ///< Transcript rows hidden above the viewport.
};

/// What a full-screen draw produced: the rows and where the caret goes.
struct RenderResult {
    std::string text;         ///< Rows joined by \n, each padded to the full width.
    std::size_t caretRow{0};  ///< Row of the caret, 0-based.
    std::size_t caretCol{0};  ///< Column of the caret, 0-based.
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

/// Renders the whole screen: a viewport of the conversation, the autocomplete hints when a
/// command is being typed, a status line, and the fixed prompt at the bottom.
/// @param state What to draw.
/// @param width Columns available.
/// @param height Rows available.
/// @return The rows and where the caret sits.
RenderResult renderScreen(const ScreenState& state, std::size_t width, std::size_t height);

/// The commands the client understands, in the order Tab cycles through them.
const std::vector<std::string>& commands();

/// The commands that start with what has been typed so far.
/// @param draft The draft.
/// @param cursor Where the caret is, in characters.
/// @return The matches, empty when the caret is not inside a command.
std::vector<std::string> commandMatches(const std::string& draft, std::size_t cursor);

/// What Tab produces inside a command: the completed draft and the new cursor.
/// @param draft The draft.
/// @param cursor Where the caret is, in characters.
/// @return The new draft and the new cursor, or the same ones when nothing completes.
std::pair<std::string, std::size_t> completeCommand(const std::string& draft, std::size_t cursor);

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
    Complete,         ///< Complete the command being typed.
    Cancel,           ///< Cancel the turn in flight.
    ClearDraft,       ///< Empty the draft.
    Leave,            ///< End the session.
    HistoryPrevious,  ///< Walk the prompts backwards.
    HistoryNext,      ///< Walk the prompts forwards.
    ScrollUp,         ///< One page of transcript up.
    ScrollDown,       ///< One page of transcript down.
};

/// Maps a key to the action it triggers.
/// @param key The key.
/// @param busy Whether a turn is in flight.
/// @param draftEmpty Whether the draft holds no text.
/// @param command Whether the draft is a command.
/// @return The action. Inserting is the caller's job, with the key's character.
Action mapKey(const Key& key, bool busy, bool draftEmpty, bool command);

/// How long a turn has been running, in tenths of a second.
/// @param state The live region.
/// @return Tenths of a second, or 0 when nothing is running.
std::size_t elapsedTenths(const ScreenState& state);

}  // namespace OpenAnimaTui
