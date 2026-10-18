#include "model.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

#include "commands.h"
#include "domain/texts.h"

namespace OpenAnimaTui::Domain {

namespace {

/// Rows one notch of the wheel moves the conversation, which is enough to follow what is being
/// written without losing the line being read.
constexpr std::size_t WheelStep = 1;

/// Trailing spaces off a submitted line, so a completed command still matches.
std::string trimRight(std::string text) {
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

/// Writes a muted note into the transcript on screen.
void note(Model& model, std::string text) {
    model.messages.push_back({Role::System, std::move(text)});
}

/// The prompts of the scope being typed at: the active NPC's, or the lobby's.
History& currentHistory(Model& model) {
    return model.world.active.has_value() ? model.world.npcs.at(*model.world.active).history
                                          : model.lobby;
}

/// Puts a submitted line at the end of the scope's history.
void remember(Model& model, const std::string& line) {
    History& history = currentHistory(model);
    history.entries.push_back(line);
    history.index = history.entries.size();
    history.scratch.clear();
}

/// Refuses a command that would leave a turn behind, saying so.
/// @return True when the caller has to stop.
bool blockedByTurn(Model& model) {
    if (!model.busy) {
        return false;
    }
    note(model, texts().waitForTurn);
    return true;
}

/// Where the code point before a position starts. The draft is UTF-8, so one byte is only
/// one character until a key is pressed on an accent.
std::size_t previousCodepoint(const std::string& text, std::size_t position) {
    std::size_t start = position > 0 ? position - 1 : 0;
    while (start > 0 && (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
        --start;
    }
    return start;
}

/// Where the next code point after a position starts.
std::size_t nextCodepoint(const std::string& text, std::size_t position) {
    std::size_t next = position < text.size() ? position + 1 : text.size();
    while (next < text.size() && (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80) {
        ++next;
    }
    return next;
}

/// @param text Draft to look at.
/// @param position Where a code point starts.
/// @return Whether that code point belongs to a word: a letter, a digit, an underscore, or a
///         byte that is not ASCII, which is how an accent or an enye counts.
bool isWordAt(const std::string& text, std::size_t position) {
    if (position >= text.size()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(text[position]);
    if (first >= 0x80U) {
        return true;
    }
    return std::isalnum(first) != 0 || first == '_';
}

/// Where the word before a position starts. Blanks and whatever is not part of a word are
/// skipped first, which is what makes control and backspace take the word and the gap before
/// it rather than one character at a time.
/// @param text Draft to look at.
/// @param position Caret, in bytes.
/// @return The start of that word, or zero when there is nothing before the caret.
std::size_t previousWord(const std::string& text, std::size_t position) {
    std::size_t cursor = position;
    while (cursor > 0 && !isWordAt(text, previousCodepoint(text, cursor))) {
        cursor = previousCodepoint(text, cursor);
    }
    while (cursor > 0 && isWordAt(text, previousCodepoint(text, cursor))) {
        cursor = previousCodepoint(text, cursor);
    }
    return cursor;
}

/// Where the word after a position ends.
/// @param text Draft to look at.
/// @param position Caret, in bytes.
/// @return The end of that word, or the end of the draft when there is nothing after it.
std::size_t nextWord(const std::string& text, std::size_t position) {
    std::size_t cursor = position;
    while (cursor < text.size() && !isWordAt(text, cursor)) {
        cursor = nextCodepoint(text, cursor);
    }
    while (cursor < text.size() && isWordAt(text, cursor)) {
        cursor = nextCodepoint(text, cursor);
    }
    return cursor;
}

/// Adds one character, or a newline, at the caret.
void editInsert(Model& model, char character) {
    model.cursor = std::min(model.cursor, model.draft.size());
    model.draft.insert(model.draft.begin() + static_cast<std::ptrdiff_t>(model.cursor), character);
    ++model.cursor;
    model.hintIndex = 0;
}

/// Deletes the character before the caret, whole.
/// @param model Model to change.
/// @param wholeWord Whether control was held, which takes the word instead of the character.
void editBackspace(Model& model, bool wholeWord) {
    if (model.cursor == 0) {
        return;
    }
    const std::size_t start = wholeWord ? previousWord(model.draft, model.cursor)
                                        : previousCodepoint(model.draft, model.cursor);
    model.draft.erase(start, model.cursor - start);
    model.cursor = start;
    model.hintIndex = 0;
}

/// Deletes the character after the caret, whole.
/// @param model Model to change.
/// @param wholeWord Whether control was held, which takes the word instead of the character.
void editDelete(Model& model, bool wholeWord) {
    if (model.cursor >= model.draft.size()) {
        return;
    }
    const std::size_t end =
        wholeWord ? nextWord(model.draft, model.cursor) : nextCodepoint(model.draft, model.cursor);
    model.draft.erase(model.cursor, end - model.cursor);
    model.hintIndex = 0;
}

/// Replaces what is typed with the chosen command.
void editComplete(Model& model, const std::string& chosen) {
    const std::pair<std::string, std::size_t> completed =
        completeCommand(model.draft, model.cursor, chosen);
    model.draft = completed.first;
    model.cursor = completed.second;
    model.hintIndex = 0;
}

/// Moves the command hint selection, stopping at the ends of the list.
/// @param model The model, which holds the selection.
/// @param hints The hints that are showing.
/// @param direction Which way it goes: back to the one before when it is negative, on to the
///        one after otherwise.
void moveHint(Model& model, const std::vector<std::string>& hints, int direction) {
    if (hints.empty()) {
        return;
    }
    const std::size_t last = hints.size() - 1;
    const std::size_t index = std::min(model.hintIndex, last);
    if (direction < 0) {
        model.hintIndex = index > 0 ? index - 1 : 0;
        return;
    }
    model.hintIndex = std::min(index + 1, last);
}

/// Walks the history of the scope being typed at, putting the draft aside on the way.
void historyWalk(Model& model, int direction) {
    History& history = currentHistory(model);
    if (direction < 0) {
        if (history.index == 0) {
            return;
        }
        if (history.index == history.entries.size()) {
            history.scratch = model.draft;
        }
        --history.index;
        model.draft = history.entries[history.index];
    } else {
        if (history.index >= history.entries.size()) {
            return;
        }
        ++history.index;
        model.draft = history.index == history.entries.size() ? history.scratch
                                                              : history.entries[history.index];
        if (history.index == history.entries.size()) {
            history.scratch.clear();
        }
    }
    model.cursor = model.draft.size();
    model.hintIndex = 0;
}

/// Saves the transcript of the NPC being left and loads the one being entered.
void switchTo(Model& model, std::optional<std::size_t> npcId) {
    if (model.world.active.has_value()) {
        model.world.npcs.at(*model.world.active).transcript = model.messages;
    }
    model.world.active = npcId;
    model.messages =
        npcId.has_value() ? model.world.npcs.at(*npcId).transcript : std::vector<Message>{};
    model.draft.clear();
    model.cursor = 0;
    model.scrollOffset = 0;
    model.hintIndex = 0;
}

/// Writes the keys and the commands into the transcript, as muted notes.
void showHelp(Model& model) {
    for (const std::string& line : texts().help) {
        note(model, line);
    }
}

/// Makes an npc and starts talking to it.
void createNpc(Model& model, const std::string& persona, std::vector<Effect>& effects) {
    if (blockedByTurn(model)) {
        return;
    }
    const std::size_t npcId = model.world.nextId++;
    model.world.npcs.emplace(npcId, Npc{persona, {}, {}});
    switchTo(model, npcId);
    effects.push_back({EffectKind::CreateNpc, npcId, persona});
    note(model, fill(texts().createdNpc, {{"id", std::to_string(npcId)}, {"persona", persona}}));
}

/// Reads the id out of a command and starts talking to that npc.
void enterNpc(Model& model, const std::string& idText) {
    if (blockedByTurn(model)) {
        return;
    }
    char* end = nullptr;
    const unsigned long long npcId = std::strtoull(idText.c_str(), &end, 10);
    if (end == idText.c_str() || *end != '\0' ||
        model.world.npcs.find(npcId) == model.world.npcs.end()) {
        note(model, texts().noSuchNpc);
        return;
    }
    switchTo(model, static_cast<std::size_t>(npcId));
}

/// Lists the npcs, and which one is being talked to.
void listNpcs(Model& model) {
    if (model.world.npcs.empty()) {
        note(model, texts().noNpcs);
        return;
    }
    note(model, texts().npcListTitle);
    for (const auto& [npcId, npc] : model.world.npcs) {
        note(model, fill(texts().npcLine,
                         {{"id", std::to_string(npcId)},
                          {"here", model.world.active == npcId ? texts().npcHere : std::string{}},
                          {"persona", npc.persona}}));
    }
}

/// Runs the draft when it is a command.
void runCommand(Model& model, const std::string& rawDraft, std::vector<Effect>& effects) {
    const std::string draft = trimRight(rawDraft);

    if (draft == "/create") {
        note(model, texts().createUsage);
        return;
    }
    if (draft.rfind("/create ", 0) == 0) {
        createNpc(model, draft.substr(8), effects);
        return;
    }

    if (draft == "/npcs") {
        listNpcs(model);
        return;
    }

    if (draft == "/enter") {
        note(model, texts().enterUsage);
        return;
    }
    if (draft.rfind("/enter ", 0) == 0) {
        enterNpc(model, draft.substr(7));
        return;
    }

    if (draft == "/leave") {
        if (blockedByTurn(model)) {
            return;
        }
        switchTo(model, std::nullopt);
        note(model, texts().leftConversation);
        return;
    }

    if (draft == "/clear") {
        if (!model.world.active.has_value()) {
            note(model, texts().noNpcToClear);
            return;
        }
        model.messages.clear();
        effects.push_back({EffectKind::ClearHistory, *model.world.active, {}});
        note(model, texts().conversationDropped);
        return;
    }

    if (draft == "/raw") {
        model.raw = !model.raw;
        note(model, model.raw ? texts().rawOn : texts().rawOff);
        return;
    }
    if (draft.rfind("/raw ", 0) == 0) {
        const std::string argument = draft.substr(5);
        if (argument != "on" && argument != "off") {
            note(model, texts().rawUsage);
            return;
        }
        model.raw = argument == "on";
        note(model, model.raw ? texts().rawOn : texts().rawOff);
        return;
    }

    if (draft == "/help") {
        showHelp(model);
        return;
    }

    if (draft == "/quit") {
        model.running = false;
        return;
    }
}

/// Sends the draft: as a command when it starts with a slash, as a line to the NPC otherwise.
void submit(Model& model, std::vector<Effect>& effects) {
    if (model.draft.empty()) {
        return;
    }
    if (model.draft.back() == '\\') {
        editBackspace(model, false);
        editInsert(model, '\n');
        return;
    }

    if (model.draft.front() == '/') {
        remember(model, model.draft);
        runCommand(model, model.draft, effects);
        model.draft.clear();
        model.cursor = 0;
        model.hintIndex = 0;
        return;
    }

    // A question asked while the NPC is still answering waits in the draft.
    if (model.busy) {
        return;
    }

    if (!model.world.active.has_value()) {
        note(model, texts().noNpc);
        model.draft.clear();
        model.cursor = 0;
        return;
    }

    remember(model, model.draft);
    model.messages.push_back({Role::User, model.draft});
    effects.push_back({EffectKind::Ask, *model.world.active, model.draft});
    model.draft.clear();
    model.cursor = 0;
    model.answer.clear();
    model.cancelling = false;
    model.busy = true;
    model.startedAt = std::chrono::steady_clock::now();
    model.scrollOffset = 0;
    model.hintIndex = 0;
}

}  // namespace

/// @param kind Key that arrived.
/// @return Whether it only moves the conversation, which leaves the panel of the prompt where
///         the reader put it.
bool movesOnlyTranscript(Key::Kind kind) {
    return kind == Key::Kind::WheelUp || kind == Key::Kind::WheelDown ||
           kind == Key::Kind::PageUp || kind == Key::Kind::PageDown;
}

/// Handles a key while a turn is in flight: the one that stops it and the one that leaves, and
/// nothing else changes meaning.
/// @param model The model, changed in place.
/// @param key The key.
/// @param effects Receives the cancel, when the key asks for one.
/// @return Whether the turn in flight took the key.
bool whileBusy(Model& model, const Key& key, std::vector<Effect>& effects) {
    if (!model.busy) {
        return false;
    }
    if (key.kind == Key::Kind::Escape) {
        if (model.world.active.has_value()) {
            model.cancelling = true;
            effects.push_back({EffectKind::Cancel, *model.world.active, {}});
        }
        return true;
    }
    if (key.kind == Key::Kind::CtrlD) {
        model.running = false;
        return true;
    }
    return false;
}

std::vector<Effect> update(Model& model, const Key& key, std::size_t scrollStep) {
    std::vector<Effect> effects;
    const std::vector<std::string> hints = hintsFor(model);

    // A turn in flight takes the key that stops it, and nothing else changes meaning. Ctrl+C is
    // not that key: it copies, and the shell takes it before it ever gets here.
    if (whileBusy(model, key, effects)) {
        return effects;
    }

    // Writing or moving the caret brings the panel back to it: a window scrolled away from the
    // caret is only worth keeping while nothing is being typed.
    if (!movesOnlyTranscript(key.kind)) {
        model.promptScroll.reset();
    }

    switch (key.kind) {
    case Key::Kind::Character:
        editInsert(model, key.character);
        break;

    case Key::Kind::Tab:
        // Tab completes; a tab is never typed. A raw one counts as one column here and as up
        // to eight on the terminal, which is what broke the row and the caret.
        if (!hints.empty()) {
            editComplete(model, hints.at(std::min(model.hintIndex, hints.size() - 1)));
        }
        break;

    case Key::Kind::Backspace:
        editBackspace(model, key.ctrl);
        break;

    case Key::Kind::Delete:
        editDelete(model, key.ctrl);
        break;

    case Key::Kind::Enter:
        submit(model, effects);
        break;

    case Key::Kind::CtrlEnter:
    case Key::Kind::CtrlJ:
        editInsert(model, '\n');
        break;

    case Key::Kind::Escape:
    // Ctrl+C is the shell's: it is the copy key, and it never reaches the model.
    case Key::Kind::CtrlC:
        break;

    case Key::Kind::CtrlD:
        model.running = false;
        break;

    case Key::Kind::ArrowUp:
        if (!hints.empty()) {
            moveHint(model, hints, -1);
        } else {
            historyWalk(model, -1);
        }
        break;

    case Key::Kind::ArrowDown:
        if (!hints.empty()) {
            moveHint(model, hints, 1);
        } else {
            historyWalk(model, 1);
        }
        break;

    case Key::Kind::CtrlP:
        historyWalk(model, -1);
        break;

    case Key::Kind::CtrlN:
        historyWalk(model, 1);
        break;

    case Key::Kind::ArrowLeft:
    case Key::Kind::CtrlB:
        // Control and an arrow moves a word at a time, the way it does everywhere else.
        model.cursor = key.ctrl ? previousWord(model.draft, model.cursor)
                                : previousCodepoint(model.draft, model.cursor);
        break;

    case Key::Kind::ArrowRight:
    case Key::Kind::CtrlF:
        model.cursor = key.ctrl ? nextWord(model.draft, model.cursor)
                                : nextCodepoint(model.draft, model.cursor);
        break;

    case Key::Kind::CtrlA:
        model.cursor = 0;
        break;

    case Key::Kind::CtrlE:
        model.cursor = model.draft.size();
        break;

    case Key::Kind::PageUp:
        model.scrollOffset += scrollStep;
        break;

    case Key::Kind::PageDown:
        model.scrollOffset = model.scrollOffset > scrollStep ? model.scrollOffset - scrollStep : 0;
        break;

    // The wheel belongs to the conversation, not to the prompt: it moves the viewport and never
    // touches the draft or the history, which is what the arrows are for.
    case Key::Kind::WheelUp:
        model.scrollOffset += WheelStep;
        break;

    case Key::Kind::WheelDown:
        model.scrollOffset = model.scrollOffset > WheelStep ? model.scrollOffset - WheelStep : 0;
        break;

    case Key::Kind::None:
    case Key::Kind::Unknown:
    // The mouse is the shell's business: it is the only part that knows where things are on the
    // screen, which is what a selection is about. A paste of what the client copied last arrives
    // as the text itself, so this key has nothing to do here either.
    case Key::Kind::MousePress:
    case Key::Kind::MouseDrag:
    case Key::Kind::MouseRelease:
    case Key::Kind::MouseMove:
    case Key::Kind::CtrlV:
        break;
    }

    return effects;
}

/// Writes the turn as the document the model wrote, which is all raw mode shows.
///
/// The painted reply is thrown away, but who answered is not: the name of the npc goes over
/// the document, so a reader knows whose turn it is without reading the json for a clue.
/// @param model Model to write into.
/// @param event Finished event of the turn.
void noteRawTurn(Model& model, const Event& event) {
    const std::string speaker =
        model.world.active.has_value()
            ? fill(texts().npcName, {{"id", std::to_string(*model.world.active)}})
            : texts().npcNameLobby;
    model.messages.push_back({Role::Speaker, speaker + ":"});
    if (!event.reply.has_value() || event.reply->document.is_null()) {
        // A turn without a document was stopped before it was whole, so the note that
        // closes a cancelled turn is the only thing there is to show.
        note(model, texts().cancelled);
        return;
    }
    note(model, event.reply->document.dump(2));
}

/// Writes a finished turn into the transcript as one block: the thought opens it and carries
/// the name of the npc, and what it says and does follow it without a name of their own.
/// @param model Model to write into.
/// @param event Finished event of the turn.
void noteTurn(Model& model, const Event& event) {
    bool opened = false;
    if (event.reply.has_value() && !event.reply->reasoning.empty()) {
        model.messages.push_back({Role::Thought, "~ " + event.reply->reasoning});
        opened = true;
    }
    if (!model.answer.empty()) {
        model.messages.push_back({Role::Npc, model.answer, opened});
        opened = true;
    }
    if (event.reply.has_value()) {
        for (const OpenAnima::Core::NpcAction& action : event.reply->actions) {
            std::string line = "[" + action.name + "]";
            if (!action.arguments.empty()) {
                line += " ";
                line += action.arguments.dump();
            }
            model.messages.push_back({Role::System, line, opened});
            opened = true;
        }
    }
}

bool applyEvent(Model& model, const Event& event) {
    switch (event.kind) {
    case OpenAnima::Core::EventKind::Started:
        return false;
    case OpenAnima::Core::EventKind::Delta:
        // Raw mode shows the turn once it is over, so the dialogue is not painted while it
        // arrives: the document is the only thing that goes in.
        if (model.raw) {
            return false;
        }
        model.answer += event.text;
        return true;
    case OpenAnima::Core::EventKind::Finished: {
        model.busy = false;
        model.cancelling = false;
        // A cancelled turn leaves nothing of what it had written, in raw mode and out of it:
        // the partial answer is dropped and the note is the only thing that stays.
        if (event.completion.finishReason == OpenAnima::Core::FinishReason::Cancelled) {
            model.answer.clear();
            note(model, texts().cancelled);
            return true;
        }
        if (model.raw) {
            noteRawTurn(model, event);
        } else {
            noteTurn(model, event);
        }
        model.answer.clear();
        return true;
    }
    case OpenAnima::Core::EventKind::Failed:
        model.busy = false;
        model.answer.clear();
        note(model, fill(texts().turnFailed, {{"error", event.error}}));
        return true;
    }
    return false;
}

std::vector<std::string> hintsFor(const Model& model) {
    return commandMatches(model.draft, model.cursor);
}

std::size_t elapsedTenths(const Model& model) {
    if (!model.busy) {
        return 0;
    }
    const auto elapsed = std::chrono::steady_clock::now() - model.startedAt;
    return static_cast<std::size_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 100);
}

}  // namespace OpenAnimaTui::Domain
