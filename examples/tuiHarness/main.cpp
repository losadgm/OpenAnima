/// @file
/// @brief The terminal client: the shell that joins the model, the screen and the brains.
///
/// The rules live in `app` (pure: keys in, effects out), the drawing in `screen` (pure: model
/// in, rows out) and the pointer in `shell` (pure: positions and drags, given the frame that
/// was painted). What is left here is the imperative part: read a key, run the effects it
/// asked for against the brains, fold the turn's events back into the model, repaint.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "OpenAnima/OpenAnima.h"

#include "app/model.h"
#include "common/apiSettings.h"
#include "common/dotenv.h"
#include "screen/diff.h"
#include "screen/render.h"
#include "shell/interaction.h"
#include "terminal/terminal.h"

namespace {

using OpenAnima::Core::NPCBrain;
using OpenAnima::Core::RequestId;
using OpenAnima::Provider::RestProviderConfig;
using OpenAnimaTui::Key;
using OpenAnimaTui::App::Effect;
using OpenAnimaTui::App::EffectKind;
using OpenAnimaTui::App::Model;
using OpenAnimaTui::Shell::Interaction;
using OpenAnimaTui::Shell::KeyResult;

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

/// Holds the state the loop reads and writes once per turn of the wheel of the client.
struct Session {
    Model model;                 ///< Everything the client knows.
    Interaction shell;           ///< What the pointer has done to the screen.
    std::string clipboard;       ///< What was copied last, kept for the next paste.
    bool dirty{true};            ///< Whether the screen has to be painted again.
    std::size_t drawnTenths{0};  ///< The tenth of a second the working line was drawn at.
};

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
    Session session;
    session.model.modelName = config.model;
    OpenAnimaTui::Screen::FrameWriter writer;

    RequestId turn{0};
    std::string frame;

    while (session.model.running) {
        // What the terminal has not taken yet goes first: while something is still on its way
        // there is no point in deciding what the screen looks like next, and saying it twice
        // would only queue another frame behind the one that has not arrived.
        const bool drained = terminal->flush();

        if (const auto key = terminal->readKey(std::chrono::milliseconds{20})) {
            const std::size_t scrollStep = terminal->height() / 2;
            const KeyResult result = OpenAnimaTui::Shell::handleKey(
                session.shell, session.model, *key, scrollStep, session.clipboard, *terminal);
            if (result.used) {
                session.dirty = session.dirty || result.painted;
                continue;
            }
            runEffects(
                OpenAnimaTui::Shell::applyKey(session.shell, session.model, *key, scrollStep),
                brains, config, turn);
            session.dirty = true;
        }

        // A drag that runs into an edge of the viewport keeps the text moving while the pointer
        // waits there, so a span can reach past what is on screen.
        session.dirty = OpenAnimaTui::Shell::autoscrollAtEdge(session.shell, session.model,
                                                              std::chrono::steady_clock::now()) ||
                        session.dirty;

        if (drainEvents(session.model, brains)) {
            session.dirty = true;
        }

        // The clock on the working line is the only thing that changes on its own.
        if (session.model.busy &&
            OpenAnimaTui::App::elapsedTenths(session.model) != session.drawnTenths) {
            session.dirty = true;
        }

        if (session.dirty && drained) {
            const OpenAnimaTui::Screen::RenderResult rendered =
                OpenAnimaTui::Screen::render(session.model, terminal->width(), terminal->height(),
                                             session.shell.selection, session.shell.pointer);
            // The viewport cannot hide more rows than there are. Holding the model to the
            // real range is what makes the wheel answer at once on the way back down.
            session.model.scrollOffset =
                std::min(session.model.scrollOffset, rendered.maxScrollOffset);
            writer.setCaret(rendered.caretRow, rendered.caretColumn);
            frame.clear();
            writer.emit(rendered.frame, frame);
            terminal->write(frame);
            session.shell.shape = rendered;
            session.drawnTenths = OpenAnimaTui::App::elapsedTenths(session.model);
            session.dirty = false;
        }
    }

    terminal->flush();
    terminal->leaveAlternateScreen();
    terminal->flush();
    return EXIT_SUCCESS;
}
