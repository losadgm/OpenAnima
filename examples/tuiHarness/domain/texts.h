/// @file
/// @brief The words the client says: the notes it writes, the status line and the prompt.

#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OpenAnimaTui::Domain {

/// The words of the client, read from `data/tui-texts.json` at build time.
///
/// Nothing that a reader sees is spelled out in the code: a sentence lives in the json, and
/// the code names it. A text with `{name}` in it is a template and goes through fill().
struct Texts {
    std::string waitForTurn;           ///< Note for a key typed while a turn runs.
    std::vector<std::string> help;     ///< The lines /help writes, in order.
    std::string createdNpc;            ///< `{id}` and `{persona}`.
    std::string noSuchNpc;             ///< Note for an id that is not there.
    std::string noNpcs;                ///< Note for a list with nothing in it.
    std::string npcListTitle;          ///< First line of the list.
    std::string npcLine;               ///< `{id}`, `{here}` and `{persona}`.
    std::string npcHere;               ///< What marks the active npc in the list.
    std::string createUsage;           ///< How /create is written.
    std::string enterUsage;            ///< How /enter is written.
    std::string leftConversation;      ///< Note after /leave.
    std::string noNpcToClear;          ///< Note for /clear with nobody active.
    std::string conversationDropped;   ///< Note after /clear.
    std::string rawOn;                 ///< Note after /raw on.
    std::string rawOff;                ///< Note after /raw off.
    std::string rawUsage;              ///< How /raw is written.
    std::string noNpc;                 ///< Note for a line with nobody to answer it.
    std::string turnFailed;            ///< `{error}`.
    std::string cancelled;             ///< Note that closes a cancelled turn.
    std::string noDocument;            ///< Note for a raw turn that has no document.
    std::string npcName;               ///< `{id}`.
    std::string npcNameLobby;          ///< Name of the npc outside a conversation.
    std::string speakerYou;            ///< Name the player is shown under.
    std::string placeholderLobby;      ///< What the empty prompt says in the lobby.
    std::string placeholderActive;     ///< What the empty prompt says in a conversation.
    std::string statusBusy;            ///< `{tenths}`.
    std::string statusCancelTail;      ///< Tail of the busy status while it runs.
    std::string statusCancellingTail;  ///< Tail of the busy status once it is stopping.
    std::string statusHintActive;      ///< What can be done in a conversation.
    std::string statusHintLobby;       ///< What can be done in the lobby.
    std::string rawLabel;              ///< What the status adds in raw mode.
};

/// @return The texts, read once from what the build embedded.
[[nodiscard]] const Texts& texts();

/// Fills the placeholders of a text.
/// @param pattern Text with placeholders such as `{name}`.
/// @param values Each placeholder and what replaces it.
/// @return The text with every placeholder replaced.
[[nodiscard]] std::string
fill(std::string_view pattern,
     std::initializer_list<std::pair<std::string_view, std::string>> values);

}  // namespace OpenAnimaTui::Domain
