#include "brains.h"

#include <map>
#include <utility>

#include "OpenAnima/OpenAnima.h"

#include "domain/model.h"

namespace OpenAnimaTui::Application {

namespace {

constexpr const char* PersonaPrefix = "You are ";
constexpr const char* PersonaSuffix = ". Answer in character.";

}  // namespace

Brains::Brains(OpenAnima::Provider::RestProviderConfig config) : m_config{std::move(config)} {}

void Brains::run(const std::vector<Domain::Effect>& effects) {
    for (const Domain::Effect& effect : effects) {
        switch (effect.kind) {
        case Domain::EffectKind::CreateNpc: {
            OpenAnima::Core::NPCBrain brain = OpenAnima::makeNpc(m_config);
            brain.setSystemPrompt(PersonaPrefix + effect.text + PersonaSuffix);
            m_brains.insert_or_assign(effect.npcId, std::move(brain));
            break;
        }
        case Domain::EffectKind::Ask:
            m_turn = m_brains.at(effect.npcId).askTurnAsync(effect.text);
            break;
        case Domain::EffectKind::Cancel:
            m_brains.at(effect.npcId).cancel(m_turn);
            break;
        case Domain::EffectKind::ClearHistory:
            m_brains.at(effect.npcId).clearHistory();
            break;
        }
    }
}

bool Brains::drain(Domain::Model& model) {
    if (!model.world.active.has_value()) {
        return false;
    }
    bool dirty = false;
    OpenAnima::Core::NPCBrain& brain = m_brains.at(*model.world.active);
    while (const auto event = brain.poll()) {
        if (Domain::applyEvent(model, *event)) {
            dirty = true;
        }
    }
    return dirty;
}

}  // namespace OpenAnimaTui::Application
