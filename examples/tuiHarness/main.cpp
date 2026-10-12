/// @file
/// @brief The terminal client: a conversation with the NPC, the answer arriving as it is
/// written, and the keys alive the whole time.
///
/// The screen logic is in `screen.h` and the terminal is in `terminal.h`; what is left here
/// is the loop that joins them: read a key, apply it, drain the turn's events, repaint.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
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
using OpenAnimaTui::Role;
using OpenAnimaTui::ScreenState;

constexpr const char* NpcName = "Aldric";
constexpr const char* Muted = "\x1b[38;2;128;128;128m";  ///< OpenCode's muted gray, #808080.
constexpr const char* Reset = "\x1b[0m";

/// Erases the live region, leaving the cursor where the frame started.
void eraseLive(OpenAnimaTui::Terminal& terminal, ScreenState& state) {
    if (state.liveLines == 0) {
        return;
    }
    terminal.write("\x1b[" + std::to_string(state.liveLines) + "A\r\x1b[J");
    state.liveLines = 0;
}

/// Prints a line for good, so it stays in the scrollback.
void printCommitted(OpenAnimaTui::Terminal& terminal, const std::string& speaker,
                    const std::string& text, Role role) {
    for (const std::string& line :
         OpenAnimaTui::renderLine(speaker, text, terminal.width(), role)) {
        terminal.write(line + "\n");
    }
}

/// Prints a dim note, for what the client itself has to say.
void printNote(OpenAnimaTui::Terminal& terminal, const std::string& text) {
    terminal.write(std::string(Muted) + text + Reset + "\n");
}

/// Erases the live region and paints it again, leaving the cursor at the draft caret.
void draw(OpenAnimaTui::Terminal& terminal, ScreenState& state) {
    eraseLive(terminal, state);

    const std::string frame = OpenAnimaTui::renderFrame(state, terminal.width());
    terminal.write("\x1b[J" + frame);

    const auto [upRows, leftColumns] = OpenAnimaTui::caretOffset(state, terminal.width());
    if (upRows != 0) {
        terminal.write("\x1b[" + std::to_string(upRows) + "A");
    }
    if (leftColumns != 0) {
        terminal.write("\x1b[" + std::to_string(leftColumns) + "D");
    }

    state.liveLines = static_cast<std::size_t>(std::count(frame.begin(), frame.end(), '\n'));
}

/// What a `/command` meant: whether it was one, and whether it ends the session.
struct CommandResult {
    bool handled{false};
    bool leave{false};
};

/// Runs the draft when it is a command.
/// @param terminal Where to write the answer.
/// @param brain The conversation, for `/history clear`.
/// @param draft The line just submitted.
CommandResult runCommand(OpenAnimaTui::Terminal& terminal, NPCBrain& brain,
                         const std::string& draft) {
    if (draft == "/clear") {
        terminal.write("\x1b[2J\x1b[H");
        return {true, false};
    }
    if (draft == "/history clear") {
        brain.clearHistory();
        printNote(terminal, "(conversation dropped, the persona stays)");
        return {true, false};
    }
    if (draft == "/help") {
        printNote(terminal, "keys:");
        printNote(terminal, "  enter                 send the line");
        printNote(terminal, "  ctrl+j, trailing \\    new line instead of sending");
        printNote(terminal, "  up, down              walk the prompts of this session");
        printNote(terminal, "  esc, ctrl+c           cancel the turn in flight");
        printNote(terminal, "  ctrl+c when idle      clear the line, and leave when it is empty");
        printNote(terminal, "  ctrl+d                leave");
        printNote(terminal, "commands:");
        printNote(terminal, "  /clear                empty the screen, keep the conversation");
        printNote(terminal, "  /history clear        drop the conversation, keep the persona");
        printNote(terminal, "  /help                 this list");
        printNote(terminal, "  /quit                 leave");
        return {true, false};
    }
    if (draft == "/quit") {
        return {true, true};
    }
    return {false, false};
}

/// Applies one key to the draft, the history and the brain.
/// @return True when the screen has to be repainted.
// NOLINTBEGIN(bugprone-easily-swappable-parameters) -- one function for one key, on purpose.
bool applyKey(OpenAnimaTui::Terminal& terminal, ScreenState& state, NPCBrain& brain,
              const OpenAnimaTui::Key& key, std::vector<std::string>& history,
              std::size_t& historyIndex, std::string& scratch, RequestId& turn, bool& running) {
    const Action action = OpenAnimaTui::mapKey(key, state.busy, state.draft.empty());

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

    case Action::Submit:
        if (state.busy || state.draft.empty()) {
            return false;
        }
        if (state.draft.back() == '\\') {
            state.draft.pop_back();
            state.draft.push_back('\n');
            state.cursor = state.draft.size();
            return true;
        }

        // The frame with the typed line goes away before anything is written under it,
        // whether the line is a command or a question.
        eraseLive(terminal, state);

        if (const CommandResult command = runCommand(terminal, brain, state.draft);
            command.handled) {
            running = !command.leave;
            state.draft.clear();
            state.cursor = 0;
            return true;
        }

        printCommitted(terminal, "you", state.draft, Role::User);
        history.push_back(state.draft);
        historyIndex = history.size();
        scratch.clear();
        turn = brain.askAsync(state.draft);
        state.draft.clear();
        state.cursor = 0;
        state.answer.clear();
        state.cancelled = false;
        state.busy = true;
        state.startedAt = std::chrono::steady_clock::now();
        return true;

    case Action::Cancel:
        brain.cancel(turn);
        return true;

    case Action::ClearDraft:
        state.draft.clear();
        state.cursor = 0;
        return true;

    case Action::Leave:
        running = false;
        return false;

    case Action::HistoryPrevious:
        if (historyIndex == 0) {
            return false;
        }
        if (historyIndex == history.size()) {
            scratch = state.draft;
        }
        --historyIndex;
        state.draft = history[historyIndex];
        state.cursor = state.draft.size();
        return true;

    case Action::HistoryNext:
        if (historyIndex >= history.size()) {
            return false;
        }
        ++historyIndex;
        state.draft = historyIndex == history.size() ? scratch : history[historyIndex];
        if (historyIndex == history.size()) {
            scratch.clear();
        }
        state.cursor = state.draft.size();
        return true;
    }

    return false;
}
// NOLINTEND(bugprone-easily-swappable-parameters)

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

    NPCBrain brain = OpenAnima::makeNpc(config);
    brain.setSystemPrompt("You are Aldric, a blacksmith in a small village. Answer in character.");

    std::unique_ptr<OpenAnimaTui::Terminal> terminal = OpenAnimaTui::makeTerminal();

    ScreenState state;
    state.model = config.model;
    state.hint = "ask something   /help for the keys";

    std::vector<std::string> history;
    std::size_t historyIndex = 0;
    std::string scratch;
    RequestId turn{0};
    bool running = true;

    draw(*terminal, state);
    std::size_t drawnTenths = 0;

    while (running) {
        bool dirty = false;

        if (const auto key = terminal->readKey(std::chrono::milliseconds{20})) {
            dirty = applyKey(*terminal, state, brain, *key, history, historyIndex, scratch, turn,
                             running);
        }

        while (const auto event = brain.poll()) {
            switch (event->kind) {
            case EventKind::Started:
                break;
            case EventKind::Delta:
                state.answer += event->text;
                dirty = true;
                break;
            case EventKind::Finished:
                state.cancelled = event->completion.finishReason == FinishReason::Cancelled;
                state.busy = false;
                eraseLive(*terminal, state);
                printCommitted(*terminal, NpcName, state.answer, Role::Npc);
                if (state.cancelled) {
                    printNote(*terminal, "  (cancelled, what it had is kept)");
                }
                state.answer.clear();
                dirty = true;
                break;
            case EventKind::Failed:
                state.busy = false;
                eraseLive(*terminal, state);
                printCommitted(*terminal, NpcName, "[" + event->error + "]", Role::Npc);
                state.answer.clear();
                dirty = true;
                break;
            }
        }

        // The clock on the working line is the only thing that changes on its own.
        if (state.busy && OpenAnimaTui::elapsedTenths(state) != drawnTenths) {
            dirty = true;
        }

        if (dirty && running) {
            draw(*terminal, state);
            drawnTenths = OpenAnimaTui::elapsedTenths(state);
        }
    }

    eraseLive(*terminal, state);
    terminal->write("\n");
    return EXIT_SUCCESS;
}
