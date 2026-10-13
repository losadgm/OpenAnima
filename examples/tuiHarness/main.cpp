/// @file
/// @brief The terminal client: the shell that joins the model, the screen and the brains.
///
/// The rules live in `app` (pure: keys in, effects out) and the drawing in `screen` (pure:
/// model in, rows out). What is left here is the imperative part: read a key, run the effects
/// it asked for against the brains, fold the turn's events back into the model, repaint.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "OpenAnima/OpenAnima.h"

#include "app/model.h"
#include "dotenv.h"
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
            turn = brains.at(effect.npcId).askAsync(effect.text);
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
/// @param painted The frame on screen.
/// @param clipboard What was copied last.
/// @param model The model, for the paste and for the step a scroll takes.
/// @param terminal Terminal the clipboard sequence goes to.
/// @return Whether the key was used here, so the model does not see it as well.
bool handleScreenKey(const Key& key, OpenAnimaTui::Screen::Selection& selection,
                     const OpenAnimaTui::Screen::Canvas& painted, std::string& clipboard,
                     Model& model, OpenAnimaTui::Terminal& terminal) {
    const std::size_t scrollStep = terminal.height() / 2;
    switch (key.kind) {
    case Key::Kind::MousePress:
        selection.begin(key.row, key.column);
        return true;
    case Key::Kind::MouseDrag:
        // Dragging back over where the button went down is part of a drag, and the paint
        // follows it down to the one cell it covers.
        selection.extend(key.row, key.column);
        return true;
    case Key::Kind::MouseRelease:
        selection.extend(key.row, key.column);
        // A click that went nowhere picks nothing out: there is no selection to copy or to show.
        if (selection.empty()) {
            selection.clear();
        }
        return true;
    case Key::Kind::CtrlC:
        if (selection.empty()) {
            return false;  // Nothing picked out: the model cancels, clears or leaves.
        }
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
        config = RestProviderConfig::fromEnvironment();
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
    OpenAnimaTui::Screen::Canvas painted{terminal->width(), terminal->height()};
    std::string clipboard;

    while (model.running) {
        // What the terminal has not taken yet goes first: while something is still on its way
        // there is no point in deciding what the screen looks like next, and saying it twice
        // would only queue another frame behind the one that has not arrived.
        const bool drained = terminal->flush();

        if (const auto key = terminal->readKey(std::chrono::milliseconds{20})) {
            if (!handleScreenKey(*key, selection, painted, clipboard, model, *terminal)) {
                runEffects(OpenAnimaTui::App::update(model, *key, terminal->height() / 2), brains,
                           config, turn);
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
                model, terminal->width(), terminal->height(), selection);
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
