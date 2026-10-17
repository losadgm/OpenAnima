/// @file
/// @brief The terminal client: the shell that joins the model, the screen and the brains.
///
/// The rules live in `app` (pure: keys in, effects out) and the drawing in `screen` (pure:
/// model in, rows out). What is left here is the imperative part: read a key, run the effects
/// it asked for against the brains, fold the turn's events back into the model, repaint.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "OpenAnima/OpenAnima.h"

#include "app/model.h"
#include "common/apiSettings.h"
#include "common/dotenv.h"
#include "screen/diff.h"
#include "screen/render.h"
#include "screen/selection.h"
#include "terminal/terminal.h"

namespace {

using OpenAnima::Core::NPCBrain;
using OpenAnima::Core::RequestId;
using OpenAnima::Provider::RestProviderConfig;
using OpenAnimaTui::Key;
using OpenAnimaTui::App::Effect;
using OpenAnimaTui::App::EffectKind;
using OpenAnimaTui::App::Model;

constexpr const char* PersonaPrefix = "You are ";
constexpr const char* PersonaSuffix = ". Answer in character.";

/// Carries out what the core asked for. This is the only place that touches a brain.
/// @param effects What to do, in order.
/// @param brains The brains, one per NPC, keyed by the id the model gave it.
/// @param config The backend every new brain is built from.
/// @param turn The turn in flight, remembered so it can be cancelled.
void runEffects(const std::vector<Effect>& effects, std::map<std::size_t, NPCBrain>& brains,
                const RestProviderConfig& config, RequestId& turn) {
    for (const Effect& effect : effects) {
        switch (effect.kind) {
        case EffectKind::CreateNpc: {
            NPCBrain brain = OpenAnima::makeNpc(config);
            brain.setSystemPrompt(PersonaPrefix + effect.text + PersonaSuffix);
            brains.insert_or_assign(effect.npcId, std::move(brain));
            break;
        }
        case EffectKind::Ask:
            turn = brains.at(effect.npcId).askTurnAsync(effect.text);
            break;
        case EffectKind::Cancel:
            brains.at(effect.npcId).cancel(turn);
            break;
        case EffectKind::ClearHistory:
            brains.at(effect.npcId).clearHistory();
            break;
        }
    }
}

/// Drains the events of the turn in flight into the model.
/// @param model The model.
/// @param brains The brains.
/// @return True when the screen has to be repainted.
bool drainEvents(Model& model, std::map<std::size_t, NPCBrain>& brains) {
    if (!model.world.active.has_value()) {
        return false;
    }
    bool dirty = false;
    NPCBrain& brain = brains.at(*model.world.active);
    while (const auto event = brain.poll()) {
        if (OpenAnimaTui::App::applyEvent(model, *event)) {
            dirty = true;
        }
    }
    return dirty;
}

/// Copies what the mouse picked out, to the terminal and to hand for a later paste.
/// @param selection What is picked out.
/// @param painted The frame on screen, which is where the text is read from.
/// @param rows Rows the scope of the selection has on the screen.
/// @param clipboard Where the text is kept for the next paste.
/// @param terminal Terminal the clipboard sequence goes to.
void copySelection(const OpenAnimaTui::Screen::Selection& selection,
                   const OpenAnimaTui::Screen::Canvas& painted, std::size_t rows,
                   std::string& clipboard, OpenAnimaTui::Terminal& terminal) {
    clipboard = OpenAnimaTui::Screen::selectedText(painted, selection, rows);
    if (clipboard.empty()) {
        return;
    }
    terminal.copyToClipboard(clipboard);
}

/// Feeds text into the draft as if it had been typed, line breaks included.
/// @param model The model.
/// @param text Text to insert.
/// @param scrollStep Rows a page scroll takes, as `update` wants.
void insertText(Model& model, const std::string& text, std::size_t scrollStep) {
    for (const char value : text) {
        const Key key =
            value == '\n' ? Key{Key::Kind::CtrlEnter, 0} : Key{Key::Kind::Character, value};
        (void)OpenAnimaTui::App::update(model, key, scrollStep);
    }
}

/// @param kind Key that arrived.
/// @return Whether it moves the transcript, which takes what was picked out with it.
bool scrollsTranscript(Key::Kind kind) {
    return kind == Key::Kind::WheelUp || kind == Key::Kind::WheelDown ||
           kind == Key::Kind::PageUp || kind == Key::Kind::PageDown;
}

/// What the pointer has done to the screen: what is picked out, where it is, and what it is
/// dragging. All of it is the shell's business, so the model never hears about a position.
struct ScreenState {
    OpenAnimaTui::Screen::Selection selection;  ///< The cells the mouse picked out.
    OpenAnimaTui::Screen::Clicks clicks;        ///< Presses counted, which a double click is.
    OpenAnimaTui::Screen::Pointer pointer;      ///< Where the pointer is.
    OpenAnimaTui::Screen::RenderResult shape;   ///< The frame on screen and where its parts sit.
    std::size_t barGrab{0};                     ///< Row of the thumb the drag holds.
    bool pullingBar{false};                     ///< Whether the bar of the conversation is dragged.
    bool pullingHintBar{false};                 ///< Whether the bar of the hints is dragged.
};

/// @param state The screen as it is.
/// @param column Column the pointer is on.
/// @param row Row the pointer is on.
/// @return Whether the pointer is over the bar of the conversation.
bool overScrollBar(const ScreenState& state, std::size_t column, std::size_t row) {
    return state.shape.maxScrollOffset > 0 && row < state.shape.scrollRows &&
           column + 1 == state.shape.frame.width();
}

/// @param state The screen as it is.
/// @param column Column the pointer is on.
/// @param row Row the pointer is on.
/// @return Whether the pointer is over the bar of the hints, which is only there when the list
///         is longer than the panel.
bool overHintBar(const ScreenState& state, std::size_t column, std::size_t row) {
    return state.shape.hintTotal > state.shape.hintCount && state.shape.hintCount > 0 &&
           column + 1 == state.shape.hintColumns && row >= state.shape.hintRow &&
           row < state.shape.hintRow + state.shape.hintCount;
}

/// @param state The screen as it is.
/// @param column Column the pointer is on.
/// @param row Row the pointer is on.
/// @return The hint the pointer is over, or nothing when it is not on the panel.
std::optional<std::size_t> hintAt(const ScreenState& state, std::size_t column, std::size_t row) {
    if (column + 1 > state.shape.hintColumns || row < state.shape.hintRow ||
        row >= state.shape.hintRow + state.shape.hintCount) {
        return std::nullopt;
    }
    return state.shape.hintOffset + (row - state.shape.hintRow);
}

/// Walks the hint selection, stopping at the ends of the list.
/// @param model The model, which holds the selection.
/// @param forward Whether it goes on to the next hint or back to the one before.
void stepHint(Model& model, bool forward) {
    const std::size_t count = OpenAnimaTui::App::hintsFor(model).size();
    if (count == 0) {
        return;
    }
    const std::size_t index = std::min(model.hintIndex, count - 1);
    if (!forward) {
        model.hintIndex = index > 0 ? index - 1 : 0;
        return;
    }
    model.hintIndex = std::min(index + 1, count - 1);
}

/// Puts the thumb of the hints where the pointer is.
/// @param state The screen as it is.
/// @param model The model, which holds the selection.
/// @param row Row the pointer is on.
void dragHintBarTo(const ScreenState& state, Model& model, std::size_t row) {
    const std::size_t offset = row >= state.shape.hintRow ? row - state.shape.hintRow : 0;
    model.hintIndex =
        OpenAnimaTui::Screen::hintAtThumbRow(state.shape.hintTotal, state.shape.hintCount, offset);
}

/// Puts the thumb of the bar where the pointer is, keeping the row it was grabbed by.
/// @param state The screen as it is.
/// @param model The model, which holds the offset.
/// @param row Row the pointer is on.
void dragBarTo(ScreenState& state, Model& model, std::size_t row) {
    const std::size_t range = state.shape.maxScrollOffset;
    const std::size_t viewRows = state.shape.scrollRows;
    if (range == 0 ||
        OpenAnimaTui::Screen::thumbRow(state.shape.transcriptRows, viewRows, range) == 0) {
        return;  // There is no room for the thumb to move, so there is nothing to drag.
    }
    const std::size_t top = row >= state.barGrab ? row - state.barGrab : 0;
    const std::size_t start =
        OpenAnimaTui::Screen::firstRowForThumb(state.shape.transcriptRows, viewRows, top);
    model.scrollOffset = range - std::min(start, range);
}

/// Takes hold of the bar: where along the thumb it was grabbed decides how it follows, and a
/// press beside the thumb brings it under the pointer.
/// @param state The screen as it is.
/// @param model The model, which holds the offset.
/// @param row Row the pointer is on.
void grabBar(ScreenState& state, Model& model, std::size_t row) {
    const std::size_t range = state.shape.maxScrollOffset;
    const std::size_t viewRows = state.shape.scrollRows;
    const std::size_t travel =
        OpenAnimaTui::Screen::thumbRow(state.shape.transcriptRows, viewRows, range);
    const std::size_t thumb = viewRows > travel ? viewRows - travel : 1;
    const std::size_t start = range - std::min(model.scrollOffset, range);
    const std::size_t top =
        OpenAnimaTui::Screen::thumbRow(state.shape.transcriptRows, viewRows, start);
    state.barGrab = (row >= top && row < top + thumb) ? row - top : thumb / 2;
    state.pullingBar = true;
    dragBarTo(state, model, row);
}

/// Watches the pointer cross the screen, which is what a hover is.
/// @param state The screen as it is.
/// @param row Row the pointer is on.
/// @param column Column the pointer is on.
// A row and a column, both named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void movePointer(ScreenState& state, std::size_t row, std::size_t column) {
    state.pointer.onScreen = true;
    state.pointer.row = row;
    state.pointer.column = column;
}

/// @param state The screen as it is.
/// @param row Row the pointer is on.
/// @return Where a selection made on that row lives: the conversation scrolls under it, and
///         the prompt and what surrounds it do not.
OpenAnimaTui::Screen::Scope scopeAt(const ScreenState& state, std::size_t row) {
    return row < state.shape.scrollRows ? OpenAnimaTui::Screen::Scope::Conversation
                                        : OpenAnimaTui::Screen::Scope::Screen;
}

/// @param state The screen as it is.
/// @param selection What is picked out.
/// @return Rows the scope of the selection has on the screen.
std::size_t rowsOf(const ScreenState& state, const OpenAnimaTui::Screen::Selection& selection) {
    return selection.scope() == OpenAnimaTui::Screen::Scope::Conversation
               ? state.shape.scrollRows
               : state.shape.frame.height();
}

/// Starts what the left button does where it went down.
/// @param key Key that arrived, which carries the position.
/// @param state What the pointer has done to the screen.
/// @param model The model, for the hints and the scroll.
/// @param scrollStep Rows a page scroll takes, as `update` wants.
void pressAt(const Key& key, ScreenState& state, Model& model, std::size_t scrollStep) {
    // The bar of the hints takes hold first: dragging it walks the list the way dragging the
    // bar of the conversation scrolls it.
    if (overHintBar(state, key.column, key.row)) {
        state.pullingHintBar = true;
        dragHintBarTo(state, model, key.row);
        return;
    }
    // A hint takes a click the way it takes tab: the one under the pointer is written into the
    // draft, and the selection is dropped rather than started over the panel.
    if (const std::optional<std::size_t> hint = hintAt(state, key.column, key.row);
        hint.has_value()) {
        model.hintIndex = *hint;
        (void)OpenAnimaTui::App::update(model, Key{Key::Kind::Tab, 0}, scrollStep);
        state.selection.clear();
        return;
    }
    if (overScrollBar(state, key.column, key.row)) {
        grabBar(state, model, key.row);
        return;
    }
    // A second press on the same cell takes the word under it and a third takes the row,
    // which is what picking text out means everywhere else. The rows of the viewport are the
    // conversation's, which scrolls; the rest of the screen stays where it is.
    const int count = state.clicks.press(key.row, key.column, std::chrono::steady_clock::now());
    const OpenAnimaTui::Screen::Scope scope = scopeAt(state, key.row);
    if (count == 2) {
        state.selection =
            OpenAnimaTui::Screen::wordAt(state.shape.frame, key.row, key.column, scope);
        return;
    }
    if (count > 2) {
        state.selection = OpenAnimaTui::Screen::lineAt(state.shape.frame, key.row, scope);
        return;
    }
    state.selection.begin(key.row, key.column, scope);
}

/// Follows the pointer with whatever it is holding: a bar, or the far end of the selection.
/// @param key Key that arrived, which carries the position.
/// @param state What the pointer has done to the screen.
/// @param model The model, which holds the scroll and the hints.
void dragTo(const Key& key, ScreenState& state, Model& model) {
    if (state.pullingHintBar) {
        dragHintBarTo(state, model, key.row);
        return;
    }
    if (state.pullingBar) {
        dragBarTo(state, model, key.row);
        return;
    }
    // A word or a row taken by a double click stays as it is: the pointer is still inside it.
    if (state.clicks.count() < 2) {
        state.selection.extend(key.row, key.column);
    }
}

/// Scrolls with the wheel or the page keys, which move whatever the pointer is over.
/// @param key Key that arrived.
/// @param state What the pointer has done to the screen.
/// @param model The model, which holds the hint selection.
/// @return Whether the hints took it, which leaves the conversation where it is.
bool scrollWith(const Key& key, const ScreenState& state, Model& model) {
    // A wheel report carries the cell it happened on; the page keys do not, so they go by the
    // last place the pointer was seen, which is the hover the panel is under.
    const bool fromWheel = key.kind == Key::Kind::WheelUp || key.kind == Key::Kind::WheelDown;
    const std::size_t row = fromWheel ? key.row : state.pointer.row;
    const std::size_t column = fromWheel ? key.column : state.pointer.column;
    if (!hintAt(state, column, row).has_value()) {
        return false;
    }
    stepHint(model, key.kind == Key::Kind::WheelDown || key.kind == Key::Kind::PageDown);
    return true;
}

/// Handles the keys that are about the screen rather than about the conversation: what the
/// pointer lights up, what it picks out, and what is copied and pasted.
/// @param key Key that arrived.
/// @param state What the pointer has done to the screen.
/// @param clipboard What was copied last.
/// @param model The model, for the hints, the paste and the step a scroll takes.
/// @param terminal Terminal the clipboard sequence goes to.
/// @return Whether the key was used here, so the model does not see it as well.
bool handleScreenKey(const Key& key, ScreenState& state, std::string& clipboard, Model& model,
                     OpenAnimaTui::Terminal& terminal) {
    const std::size_t scrollStep = terminal.height() / 2;

    switch (key.kind) {
    case Key::Kind::MousePress:
    case Key::Kind::MouseDrag:
    case Key::Kind::MouseRelease:
    case Key::Kind::MouseMove:
        movePointer(state, key.row, key.column);
        break;
    default:
        break;
    }

    switch (key.kind) {
    case Key::Kind::MousePress:
        pressAt(key, state, model, scrollStep);
        return true;
    case Key::Kind::MouseDrag:
        dragTo(key, state, model);
        return true;
    case Key::Kind::MouseRelease:
        if (state.pullingHintBar) {
            state.pullingHintBar = false;
            return true;
        }
        if (state.pullingBar) {
            state.pullingBar = false;
            return true;
        }
        dragTo(key, state, model);
        return true;
    case Key::Kind::MouseMove:
        // Only what is under the pointer can have changed, and a frame that would look the same
        // is never written.
        return true;
    case Key::Kind::WheelUp:
    case Key::Kind::WheelDown:
    case Key::Kind::PageUp:
    case Key::Kind::PageDown:
        return scrollWith(key, state, model);
    case Key::Kind::CtrlC:
        // The copy key, and nothing else. A client that leaves on it is a client someone leaves
        // by accident while reaching for a selection.
        copySelection(state.selection, state.shape.frame, rowsOf(state, state.selection), clipboard,
                      terminal);
        state.selection.clear();
        return true;
    case Key::Kind::CtrlV:
        if (clipboard.empty()) {
            return true;  // Nothing to paste, and the model has no use for the key either.
        }
        insertText(model, clipboard, scrollStep);
        state.selection.clear();
        return true;
    default:
        return false;
    }
}

}  // namespace

/// Runs the client.
/// @return EXIT_SUCCESS, or 2 when the environment is not configured.
int main() {
    const std::string dotEnvPath = OpenAnimaExample::findDotEnv();
    OpenAnimaExample::applyDotEnv(dotEnvPath);

    RestProviderConfig config;
    try {
        config = OpenAnimaExample::readApiSettings("OPENANIMA_TUI");
    } catch (const OpenAnima::OpenAnimaError& error) {
        std::cerr << "configuration error: " << error.what() << '\n';
        return 2;
    }

    std::unique_ptr<OpenAnimaTui::Terminal> terminal = OpenAnimaTui::makeTerminal();
    terminal->enterAlternateScreen();

    std::map<std::size_t, NPCBrain> brains;
    Model model;
    model.modelName = config.model;
    OpenAnimaTui::Screen::FrameWriter writer;

    RequestId turn{0};
    bool dirty = true;
    std::size_t drawnTenths = 0;
    std::string frame;

    // What the pointer is doing, and what the client copied last. Both are about the screen,
    // which is the shell's business: the model never hears about a position.
    ScreenState state;
    std::string clipboard;

    while (model.running) {
        // What the terminal has not taken yet goes first: while something is still on its way
        // there is no point in deciding what the screen looks like next, and saying it twice
        // would only queue another frame behind the one that has not arrived.
        const bool drained = terminal->flush();

        if (const auto key = terminal->readKey(std::chrono::milliseconds{20})) {
            const OpenAnimaTui::Screen::Pointer before = state.pointer;
            if (handleScreenKey(*key, state, clipboard, model, *terminal)) {
                // The pointer crossing a cell with nothing under it changes nothing on screen.
                const bool hoverMoved = before.onScreen != state.pointer.onScreen ||
                                        before.row != state.pointer.row ||
                                        before.column != state.pointer.column;
                dirty = dirty || key->kind != Key::Kind::MouseMove || hoverMoved;
                continue;
            }
            // Anything else is the reader moving on: what was picked out is dropped, so a
            // selection never sits there over text that has changed under it. The wheel and the
            // page keys are the exception: they move the conversation, and what was picked out
            // goes with the words it covers.
            const bool scrolls = scrollsTranscript(key->kind);
            if (!scrolls) {
                state.selection.clear();
            }
            const std::size_t offsetBefore = model.scrollOffset;
            runEffects(OpenAnimaTui::App::update(model, *key, terminal->height() / 2), brains,
                       config, turn);
            if (scrolls) {
                // The model asks for the rows, and the viewport gives what it has: the picking
                // follows the rows that were really shown, not the ones that were asked for.
                const std::size_t offsetAfter =
                    std::min(model.scrollOffset, state.shape.maxScrollOffset);
                model.scrollOffset = offsetAfter;
                state.selection.shift(static_cast<std::ptrdiff_t>(offsetAfter) -
                                      static_cast<std::ptrdiff_t>(offsetBefore));
            }
            dirty = true;
        }

        if (drainEvents(model, brains)) {
            dirty = true;
        }

        // The clock on the working line is the only thing that changes on its own.
        if (model.busy && OpenAnimaTui::App::elapsedTenths(model) != drawnTenths) {
            dirty = true;
        }

        if (dirty && drained) {
            const OpenAnimaTui::Screen::RenderResult rendered = OpenAnimaTui::Screen::render(
                model, terminal->width(), terminal->height(), state.selection, state.pointer);
            // The viewport cannot hide more rows than there are. Holding the model to the
            // real range is what makes the wheel answer at once on the way back down.
            model.scrollOffset = std::min(model.scrollOffset, rendered.maxScrollOffset);
            writer.setCaret(rendered.caretRow, rendered.caretColumn);
            frame.clear();
            writer.emit(rendered.frame, frame);
            terminal->write(frame);
            state.shape = rendered;
            drawnTenths = OpenAnimaTui::App::elapsedTenths(model);
            dirty = false;
        }
    }

    terminal->flush();
    terminal->leaveAlternateScreen();
    terminal->flush();
    return EXIT_SUCCESS;
}
