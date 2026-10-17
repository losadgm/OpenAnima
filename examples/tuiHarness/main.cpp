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
#include "screen/menu.h"
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
/// @param clipboard Where the text is kept for the next paste.
/// @param terminal Terminal the clipboard sequence goes to.
void copySelection(const OpenAnimaTui::Screen::Selection& selection,
                   const OpenAnimaTui::Screen::Canvas& painted, std::string& clipboard,
                   OpenAnimaTui::Terminal& terminal) {
    clipboard = OpenAnimaTui::Screen::selectedText(painted, selection);
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

/// Handles the keys that are about the screen rather than about the conversation: picking text
/// out with the mouse, copying it and pasting what was copied last.
/// @param key Key that arrived.
/// @param selection What the mouse has picked out so far.
/// @param clicks Presses counted so far, which is what tells a double click from two clicks.
/// @param painted The frame on screen.
/// @param clipboard What was copied last.
/// @param model The model, for the paste and for the step a scroll takes.
/// @param terminal Terminal the clipboard sequence goes to.
/// @param menu The menu the right button opened.
/// @return Whether the key was used here, so the model does not see it as well.
bool handleScreenKey(const Key& key, OpenAnimaTui::Screen::Selection& selection,
                     OpenAnimaTui::Screen::Clicks& clicks,
                     const OpenAnimaTui::Screen::Canvas& painted, std::string& clipboard,
                     Model& model, OpenAnimaTui::Terminal& terminal,
                     OpenAnimaTui::Screen::Menu& menu) {
    const std::size_t scrollStep = terminal.height() / 2;
    const std::size_t entries = OpenAnimaTui::Screen::MenuEntries.size();

    // While the menu is up it takes the keys: the arrows walk it, Return or a click on an entry
    // picks it, and anything else puts it away and then does its usual job.
    if (menu.open) {
        switch (key.kind) {
        case Key::Kind::ArrowUp:
            menu.selected = menu.selected == 0 ? entries - 1 : menu.selected - 1;
            return true;
        case Key::Kind::ArrowDown:
            menu.selected = (menu.selected + 1) % entries;
            return true;
        case Key::Kind::Escape:
            menu.open = false;
            return true;
        case Key::Kind::MouseRightPress:
            menu.placement = OpenAnimaTui::Screen::placeMenu(key.row, key.column, painted.width(),
                                                             painted.height());
            menu.selected = 0;
            return true;
        case Key::Kind::MouseRightRelease:
            return true;  // The button coming back up leaves the menu where it is.        case
                          // Key::Kind::Enter:
        case Key::Kind::CtrlC:
            menu.open = false;
            if (menu.selected == 0) {
                copySelection(selection, painted, clipboard, terminal);
                selection.clear();
            } else {
                insertText(model, clipboard, scrollStep);
            }
            return true;
        case Key::Kind::MousePress: {
            const std::optional<std::size_t> entry =
                OpenAnimaTui::Screen::menuEntryAt(menu.placement, key.row, key.column);
            menu.open = false;
            if (entry == 0) {
                copySelection(selection, painted, clipboard, terminal);
                selection.clear();
            } else if (entry == 1) {
                insertText(model, clipboard, scrollStep);
            }
            return true;
        }
        default:
            menu.open = false;  // The key is not about the menu, so the menu gets out of the way.
            break;
        }
    }

    switch (key.kind) {
    case Key::Kind::MousePress: {
        // A second press on the same cell takes the word under it and a third takes the row,
        // which is what picking text out means everywhere else.
        const int count = clicks.press(key.row, key.column, std::chrono::steady_clock::now());
        if (count == 2) {
            selection = OpenAnimaTui::Screen::wordAt(painted, key.row, key.column);
        } else if (count > 2) {
            selection = OpenAnimaTui::Screen::lineAt(painted, key.row);
        } else {
            selection.begin(key.row, key.column);
        }
        return true;
    }
    case Key::Kind::MouseRightPress:
        // The terminal's own menu is out of action while the client reports the mouse, so the
        // right button opens one of its own, where the pointer is.
        menu.open = true;
        menu.placement =
            OpenAnimaTui::Screen::placeMenu(key.row, key.column, painted.width(), painted.height());
        menu.selected = 0;
        return true;
    case Key::Kind::MouseRightRelease:
        return true;  // The menu is already up, or already gone; the release says nothing.
    case Key::Kind::MouseDrag:
    case Key::Kind::MouseRelease:
        // The drag moves the far end of the selection, and the release leaves it where the
        // pointer is. A press and a release on the same cell leave that cell picked out, which
        // is the only way to take a single character. A word or a row taken by a double click
        // stays as it is: the pointer is still inside it.
        if (clicks.count() < 2) {
            selection.extend(key.row, key.column);
        }
        return true;
    case Key::Kind::CtrlC:
        // The copy key, and nothing else. A client that leaves on it is a client someone leaves
        // by accident while reaching for a selection.
        copySelection(selection, painted, clipboard, terminal);
        selection.clear();
        return true;
    case Key::Kind::CtrlV:
        if (clipboard.empty()) {
            return true;  // Nothing to paste, and the model has no use for the key either.
        }
        insertText(model, clipboard, scrollStep);
        selection.clear();
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

    // What the mouse picked out, and what the client copied last. Both are about the screen,
    // which is the shell's business: the model never hears about a position.
    OpenAnimaTui::Screen::Selection selection;
    OpenAnimaTui::Screen::Clicks clicks;
    OpenAnimaTui::Screen::Menu menu;
    OpenAnimaTui::Screen::Canvas painted{terminal->width(), terminal->height()};
    std::string clipboard;

    while (model.running) {
        // What the terminal has not taken yet goes first: while something is still on its way
        // there is no point in deciding what the screen looks like next, and saying it twice
        // would only queue another frame behind the one that has not arrived.
        const bool drained = terminal->flush();

        if (const auto key = terminal->readKey(std::chrono::milliseconds{20})) {
            if (handleScreenKey(*key, selection, clicks, painted, clipboard, model, *terminal,
                                menu)) {
                dirty = true;
                continue;
            }
            // Anything else is the reader moving on: what was picked out is dropped, so a
            // selection never sits there over text that has changed under it.
            selection.clear();
            runEffects(OpenAnimaTui::App::update(model, *key, terminal->height() / 2), brains,
                       config, turn);
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
                model, terminal->width(), terminal->height(), selection, menu);
            // The viewport cannot hide more rows than there are. Holding the model to the
            // real range is what makes the wheel answer at once on the way back down.
            model.scrollOffset = std::min(model.scrollOffset, rendered.maxScrollOffset);
            writer.setCaret(rendered.caretRow, rendered.caretColumn);
            frame.clear();
            writer.emit(rendered.frame, frame);
            terminal->write(frame);
            painted = rendered.frame;
            drawnTenths = OpenAnimaTui::App::elapsedTenths(model);
            dirty = false;
        }
    }

    terminal->flush();
    terminal->leaveAlternateScreen();
    terminal->flush();
    return EXIT_SUCCESS;
}
