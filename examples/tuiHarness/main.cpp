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
#include "screen/render.h"
#include "terminal/terminal.h"

namespace {

using OpenAnima::Core::NPCBrain;
using OpenAnima::Core::RequestId;
using OpenAnima::Provider::RestProviderConfig;
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

    RequestId turn{0};
    bool dirty = true;
    std::size_t drawnTenths = 0;

    while (model.running) {
        if (const auto key = terminal->readKey(std::chrono::milliseconds{20})) {
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

        if (dirty) {
            const OpenAnimaTui::Screen::RenderResult rendered =
                OpenAnimaTui::Screen::render(model, terminal->width(), terminal->height());
            terminal->write("\x1b[H" + rendered.text);
            terminal->write("\x1b[" + std::to_string(rendered.caretRow + 1) + ";" +
                            std::to_string(rendered.caretCol + 1) + "H");
            drawnTenths = OpenAnimaTui::App::elapsedTenths(model);
            dirty = false;
        }
    }

    terminal->leaveAlternateScreen();
    return EXIT_SUCCESS;
}
