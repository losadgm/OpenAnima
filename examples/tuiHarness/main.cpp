/// @file
/// @brief The terminal client: a world of NPCs, one conversation at a time, the answer
/// arriving as it is written, and the keys alive the whole time.
///
/// The screen logic is in `screen.h` and the terminal is in `terminal.h`; what is left here
/// is the loop that joins them: read a key, apply it, drain the turn's events, repaint.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "OpenAnima/OpenAnima.h"

#include "dotenv.h"
#include "screen.h"
#include "terminal.h"

namespace {

using OpenAnima::Core::EventKind;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::NPCBrain;
using OpenAnima::Core::RequestId;
using OpenAnimaTui::Action;
using OpenAnimaTui::Message;
using OpenAnimaTui::Role;
using OpenAnimaTui::ScreenState;

/// One NPC: its brain, the persona it was made from, and the conversation it has had.
struct Npc {
    NPCBrain brain;
    std::string persona;
    std::vector<Message> transcript;
};

/// Everything the client is carrying: the screen, the NPCs, the prompt history and the loop.
struct Session {
    ScreenState state;
    OpenAnima::Provider::RestProviderConfig config;
    std::map<std::size_t, Npc> npcs;
    std::size_t nextId{1};
    std::optional<std::size_t> active;  ///< The NPC being talked to, when there is one.
    std::vector<std::string> history;
    std::size_t historyIndex{0};
    std::string scratch;
    RequestId turn{0};
    bool running{true};
};

/// Trailing spaces off a submitted line, so a completed command still matches.
std::string trimRight(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

/// Writes a muted note into the transcript the viewport shows.
void note(Session& session, std::string text) {
    session.state.messages.push_back({Role::System, std::move(text)});
}

/// Saves the transcript of the NPC being left and loads the one being entered.
/// @param npcId The NPC to switch to, or nothing to leave the conversation.
void switchTo(Session& session, std::optional<std::size_t> npcId) {
    if (session.active.has_value()) {
        session.npcs.at(*session.active).transcript = session.state.messages;
    }
    session.active = npcId;
    session.state.messages =
        npcId.has_value() ? session.npcs.at(*npcId).transcript : std::vector<Message>{};
    session.state.npcLabel = npcId.has_value() ? "npc " + std::to_string(*npcId) : std::string{};
    session.state.hint = npcId.has_value() ? "ask something   /help for keys"
                                           : "no npc   /create to make one   /help for keys";
    session.state.answer.clear();
    session.state.cancelled = false;
    session.state.busy = false;
    session.state.draft.clear();
    session.state.cursor = 0;
    session.state.scrollOffset = 0;
}

/// Writes the keys and the commands into the transcript, as muted notes.
void showHelp(Session& session) {
    note(session, "keys:");
    note(session, "  enter                 send the line");
    note(session, "  ctrl+j, trailing \\    new line instead of sending");
    note(session, "  tab                   complete the command");
    note(session, "  up, down              walk the prompts of this session");
    note(session, "  page up, page down    scroll the conversation");
    note(session, "  esc, ctrl+c           cancel the turn in flight");
    note(session, "  ctrl+c when idle      clear the line, and leave when it is empty");
    note(session, "  ctrl+d                leave");
    note(session, "commands:");
    note(session, "  /create <description> make an npc and talk to it");
    note(session, "  /enter <id>           talk to another npc");
    note(session, "  /npcs                 list the npcs");
    note(session, "  /leave                leave the conversation");
    note(session, "  /history clear        drop the active conversation, keep the persona");
    note(session, "  /help                 this list");
    note(session, "  /quit                 leave");
}

/// What a `/command` meant: whether it was one, and whether it ends the session.
struct CommandResult {
    bool handled{false};
    bool leave{false};
};

/// Refuses a command that would leave a turn behind, saying so.
/// @return True when the caller has to stop.
bool blockedByTurn(Session& session) {
    if (!session.state.busy) {
        return false;
    }
    note(session, "wait for the answer, or cancel it first");
    return true;
}

/// Makes an NPC from a description and starts talking to it.
void createNpc(Session& session, const std::string& persona) {
    const std::size_t npcId = session.nextId++;
    session.npcs.emplace(npcId, Npc{OpenAnima::makeNpc(session.config), persona, {}});
    session.npcs.at(npcId).brain.setSystemPrompt("You are " + persona + ". Answer in character.");
    switchTo(session, npcId);
    note(session, "created npc " + std::to_string(npcId) + ": " + persona);
}

/// Starts talking to the NPC named by a number written as text.
void enterNpc(Session& session, const std::string& idText) {
    char* end = nullptr;
    const unsigned long long npcId = std::strtoull(idText.c_str(), &end, 10);
    if (end == idText.c_str() || *end != '\0' || session.npcs.find(npcId) == session.npcs.end()) {
        note(session, "no such npc   /npcs to list them");
        return;
    }
    switchTo(session, static_cast<std::size_t>(npcId));
    note(session, "now talking to npc " + std::to_string(npcId));
}

/// Runs the draft when it is a command.
CommandResult runCommand(Session& session, const std::string& rawDraft) {
    const std::string draft = trimRight(rawDraft);

    if (draft == "/create") {
        note(session, "usage: /create <description>");
        return {true, false};
    }
    if (draft.rfind("/create ", 0) == 0) {
        if (!blockedByTurn(session)) {
            createNpc(session, draft.substr(8));
        }
        return {true, false};
    }

    if (draft == "/npcs") {
        if (session.npcs.empty()) {
            note(session, "no npcs yet   /create to make one");
            return {true, false};
        }
        note(session, "npcs:");
        for (const auto& [npcId, npc] : session.npcs) {
            note(session, "  npc " + std::to_string(npcId) +
                              (session.active == npcId ? "  <-- here" : "") + ": " + npc.persona);
        }
        return {true, false};
    }

    if (draft == "/enter") {
        note(session, "usage: /enter <id>");
        return {true, false};
    }
    if (draft.rfind("/enter ", 0) == 0) {
        if (!blockedByTurn(session)) {
            enterNpc(session, draft.substr(7));
        }
        return {true, false};
    }

    if (draft == "/leave") {
        if (!blockedByTurn(session)) {
            switchTo(session, std::nullopt);
            note(session, "left the conversation");
        }
        return {true, false};
    }

    if (draft == "/history clear") {
        if (!session.active.has_value()) {
            note(session, "no npc to clear");
            return {true, false};
        }
        session.npcs.at(*session.active).brain.clearHistory();
        session.npcs.at(*session.active).transcript.clear();
        session.state.messages.clear();
        note(session, "conversation dropped, the persona stays");
        return {true, false};
    }

    if (draft == "/help") {
        showHelp(session);
        return {true, false};
    }

    if (draft == "/quit") {
        return {true, true};
    }

    return {false, false};
}

/// Sends the draft: as a command when it starts with a slash, as a line to the NPC otherwise.
/// @return True when the screen has to be repainted.
bool submit(Session& session) {
    ScreenState& state = session.state;
    if (state.draft.empty()) {
        return false;
    }
    if (state.draft.back() == '\\') {
        state.draft.pop_back();
        state.draft.push_back('\n');
        state.cursor = state.draft.size();
        return true;
    }

    if (state.draft.front() == '/') {
        const CommandResult result = runCommand(session, state.draft);
        state.draft.clear();
        state.cursor = 0;
        if (result.leave) {
            session.running = false;
        }
        return true;
    }

    // A question asked while the NPC is still answering waits in the draft.
    if (state.busy) {
        return false;
    }

    if (!session.active.has_value()) {
        note(session, "no npc   /create to make one, /enter to pick one");
        state.draft.clear();
        state.cursor = 0;
        return true;
    }

    session.npcs.at(*session.active).transcript.push_back({Role::User, state.draft});
    state.messages.push_back({Role::User, state.draft});
    session.history.push_back(state.draft);
    session.historyIndex = session.history.size();
    session.scratch.clear();
    session.turn = session.npcs.at(*session.active).brain.askAsync(state.draft);
    state.draft.clear();
    state.cursor = 0;
    state.answer.clear();
    state.cancelled = false;
    state.busy = true;
    state.startedAt = std::chrono::steady_clock::now();
    state.scrollOffset = 0;
    return true;
}

/// Drains the events of the active NPC's turn.
/// @return True when the screen has to be repainted.
bool drainEvents(Session& session) {
    if (!session.active.has_value()) {
        return false;
    }
    bool dirty = false;
    NPCBrain& brain = session.npcs.at(*session.active).brain;
    while (const auto event = brain.poll()) {
        switch (event->kind) {
        case EventKind::Started:
            break;
        case EventKind::Delta:
            session.state.answer += event->text;
            dirty = true;
            break;
        case EventKind::Finished:
            session.state.cancelled = event->completion.finishReason == FinishReason::Cancelled;
            session.state.busy = false;
            if (!session.state.answer.empty()) {
                session.npcs.at(*session.active)
                    .transcript.push_back({Role::Npc, session.state.answer});
                session.state.messages.push_back({Role::Npc, session.state.answer});
            }
            if (session.state.cancelled) {
                note(session, "(cancelled, what it had is kept)");
            }
            session.state.answer.clear();
            dirty = true;
            break;
        case EventKind::Failed:
            session.state.busy = false;
            session.state.messages.push_back({Role::Npc, "[" + event->error + "]"});
            session.state.answer.clear();
            dirty = true;
            break;
        }
    }
    return dirty;
}

/// Applies one key to the draft, the history and the active brain.
/// @param session The world, the screen and the loop.
/// @param key The key.
/// @param scrollStep How many rows one page of scrolling covers.
/// @return True when the screen has to be repainted.
bool applyKey(Session& session, const OpenAnimaTui::Key& key, std::size_t scrollStep) {
    const bool command = !session.state.draft.empty() && session.state.draft.front() == '/';
    const Action action =
        OpenAnimaTui::mapKey(key, session.state.busy, session.state.draft.empty(), command);
    ScreenState& state = session.state;

    switch (action) {
    case Action::None:
        return false;

    case Action::Insert: {
        const char inserted = key.kind == OpenAnimaTui::Key::Kind::Tab ? '\t' : key.character;
        state.draft.insert(state.draft.begin() + static_cast<std::ptrdiff_t>(state.cursor),
                           inserted);
        ++state.cursor;
        return true;
    }

    case Action::Backspace:
        if (state.cursor > 0) {
            state.draft.erase(state.draft.begin() + static_cast<std::ptrdiff_t>(state.cursor - 1));
            --state.cursor;
            return true;
        }
        return false;

    case Action::MoveLeft:
        state.cursor = state.cursor > 0 ? state.cursor - 1 : 0;
        return true;

    case Action::MoveRight:
        state.cursor = std::min(state.cursor + 1, state.draft.size());
        return true;

    case Action::MoveHome:
        state.cursor = 0;
        return true;

    case Action::MoveEnd:
        state.cursor = state.draft.size();
        return true;

    case Action::Newline:
        state.draft.insert(state.draft.begin() + static_cast<std::ptrdiff_t>(state.cursor), '\n');
        ++state.cursor;
        return true;

    case Action::Complete: {
        const auto [completed, cursor] = OpenAnimaTui::completeCommand(state.draft, state.cursor);
        state.draft = completed;
        state.cursor = cursor;
        return true;
    }

    case Action::Submit:
        return submit(session);

    case Action::Cancel:
        if (session.active.has_value()) {
            session.npcs.at(*session.active).brain.cancel(session.turn);
        }
        return true;

    case Action::ClearDraft:
        state.draft.clear();
        state.cursor = 0;
        return true;

    case Action::Leave:
        session.running = false;
        return false;

    case Action::HistoryPrevious:
        if (session.historyIndex == 0) {
            return false;
        }
        if (session.historyIndex == session.history.size()) {
            session.scratch = state.draft;
        }
        --session.historyIndex;
        state.draft = session.history[session.historyIndex];
        state.cursor = state.draft.size();
        return true;

    case Action::HistoryNext:
        if (session.historyIndex >= session.history.size()) {
            return false;
        }
        ++session.historyIndex;
        state.draft = session.historyIndex == session.history.size()
                          ? session.scratch
                          : session.history[session.historyIndex];
        if (session.historyIndex == session.history.size()) {
            session.scratch.clear();
        }
        state.cursor = state.draft.size();
        return true;

    case Action::ScrollUp:
        state.scrollOffset += scrollStep;
        return true;

    case Action::ScrollDown:
        state.scrollOffset = state.scrollOffset > scrollStep ? state.scrollOffset - scrollStep : 0;
        return true;
    }

    return false;
}

}  // namespace

/// Runs the client.
/// @return EXIT_SUCCESS, or 2 when the environment is not configured.
int main() {
    const std::string dotEnvPath = OpenAnimaExample::findDotEnv();
    OpenAnimaExample::applyDotEnv(dotEnvPath);

    OpenAnima::Provider::RestProviderConfig config;
    try {
        config = OpenAnima::Provider::RestProviderConfig::fromEnvironment();
    } catch (const OpenAnima::OpenAnimaError& error) {
        std::cerr << "configuration error: " << error.what() << '\n';
        return 2;
    }

    std::unique_ptr<OpenAnimaTui::Terminal> terminal = OpenAnimaTui::makeTerminal();
    terminal->enterAlternateScreen();

    Session session;
    session.config = config;
    session.state.model = config.model;
    session.state.hint = "no npc   /create to make one   /help for keys";

    bool dirty = true;
    std::size_t drawnTenths = 0;

    while (session.running) {
        if (const auto key = terminal->readKey(std::chrono::milliseconds{20})) {
            if (applyKey(session, *key, terminal->height() / 2)) {
                dirty = true;
            }
        }

        if (drainEvents(session)) {
            dirty = true;
        }

        // The clock on the working line is the only thing that changes on its own.
        if (session.state.busy && OpenAnimaTui::elapsedTenths(session.state) != drawnTenths) {
            dirty = true;
        }

        if (dirty) {
            const OpenAnimaTui::RenderResult rendered =
                OpenAnimaTui::renderScreen(session.state, terminal->width(), terminal->height());
            terminal->write("\x1b[H" + rendered.text);
            terminal->write("\x1b[" + std::to_string(rendered.caretRow + 1) + ";" +
                            std::to_string(rendered.caretCol + 1) + "H");
            drawnTenths = OpenAnimaTui::elapsedTenths(session.state);
            dirty = false;
        }
    }

    terminal->leaveAlternateScreen();
    return EXIT_SUCCESS;
}
