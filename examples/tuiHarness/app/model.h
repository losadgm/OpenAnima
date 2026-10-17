/// @file
/// @brief The client's world and its rules, with no terminal and no brains.
///
/// This is the functional core: `update()` turns a key into model changes and a list of
/// effects, and `applyEvent()` folds a turn's event into the model. What the effects mean
/// (asking a brain, cancelling a turn) is the shell's business, which is what makes every
/// behaviour here testable without a terminal.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "OpenAnima/Core/Event.h"

#include "terminal/terminal.h"

namespace OpenAnimaTui::App {

using OpenAnima::Core::Event;

/// Who speaks a line. The colour of a role is the screen's business.
enum class Role : std::uint8_t {
    User,     ///< What the player typed.
    Npc,      ///< What the npc says out loud.
    Thought,  ///< What the npc thought first: named like the npc, painted quietly.
    System    ///< A note from the client, belonging to no speaker.
};

/// One finished turn of the conversation, oldest first.
struct Message {
    Role role;
    std::string text;
    /// Part of the message before it: no blank row between them, and no name of its own.
    /// A reply is one of these blocks, named once on the thought that opens it.
    bool continues{false};
};

/// The prompts typed in one scope (one NPC, or the lobby), and where the walk sits.
struct History {
    std::vector<std::string> entries;
    std::size_t index{0};  ///< How far back the walk is, from the newest.
    std::string scratch;   ///< The draft put aside when the walk started.
};

/// One NPC, as the client remembers it: who it is, what was said, what was typed.
struct Npc {
    std::string persona;
    std::vector<Message> transcript;
    History history;
};

/// The NPCs that exist, and which one is being talked to.
struct World {
    std::map<std::size_t, Npc> npcs;
    std::size_t nextId{1};
    std::optional<std::size_t> active;
};

/// Everything the client knows. The brains live in the shell, keyed by the same ids.
struct Model {
    World world;
    History lobby;                  ///< The prompts typed outside any conversation.
    std::vector<Message> messages;  ///< The transcript on screen: the active one.
    std::string draft;
    std::size_t cursor{0};
    std::size_t scrollOffset{0};  ///< Transcript rows hidden above the viewport.
    std::size_t hintIndex{0};     ///< Which of the command hints is selected.
    std::string answer;           ///< Text of the turn in flight.
    std::string modelName;        ///< Model that answers, for the status line.
    bool raw{false};              ///< Show every turn as the JSON the model wrote.
    bool busy{false};             ///< A turn is in flight: the NPC is writing.
    bool cancelling{false};       ///< The turn in flight was asked to stop and has not ended yet.
    bool running{true};
    std::chrono::steady_clock::time_point startedAt;
};

/// What the shell has to do, because it needs a brain to do it.
enum class EffectKind : std::uint8_t { CreateNpc, Ask, Cancel, ClearHistory };

/// One piece of work for the shell.
struct Effect {
    EffectKind kind{EffectKind::CreateNpc};
    std::size_t npcId{0};  ///< The NPC the effect is about.
    std::string text;      ///< The persona, for a creation.
};

/// Applies one key to the model.
/// @param model The model, changed in place.
/// @param key The key.
/// @param scrollStep How many rows one page of scrolling covers.
/// @return What the shell has to carry out, in order.
std::vector<Effect> update(Model& model, const Key& key, std::size_t scrollStep);

/// Folds one event of a turn into the model.
/// @param model The model, changed in place.
/// @param event The event.
/// @return True when the screen has to be repainted.
bool applyEvent(Model& model, const Event& event);

/// The commands that complete the draft right now.
/// @param model The model.
/// @return The matches, in hint order.
std::vector<std::string> hintsFor(const Model& model);

/// How long a turn has been running, in tenths of a second.
/// @param model The model.
/// @return Tenths of a second, or 0 when nothing is running.
std::size_t elapsedTenths(const Model& model);

}  // namespace OpenAnimaTui::App
