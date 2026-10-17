/// @file
/// @brief The brains the conversation runs on: one per NPC, and the turn in flight.

#pragma once

#include <cstddef>
#include <map>
#include <vector>

#include "OpenAnima/OpenAnima.h"

#include "domain/model.h"

namespace OpenAnimaTui::Application {

/// The brains the conversation runs on, one per NPC, and the turn in flight.
///
/// The model says what has to happen; this is the only part that can do it, because a brain is
/// the library's and it reaches the network. Everything here is about that boundary, and nothing
/// here knows what a screen or a pointer is.
class Brains {
public:
    /// @param config The backend every new brain is built from.
    explicit Brains(OpenAnima::Provider::RestProviderConfig config);

    /// Carries out what the model asked for, in order.
    /// @param effects What to do.
    void run(const std::vector<Domain::Effect>& effects);

    /// Drains the events of the turn in flight into the model.
    /// @param model The model, changed in place.
    /// @return True when the screen has to be repainted.
    [[nodiscard]] bool drain(Domain::Model& model);

private:
    OpenAnima::Provider::RestProviderConfig m_config;           ///< Backend of every brain.
    std::map<std::size_t, OpenAnima::Core::NPCBrain> m_brains;  ///< One per NPC, by id.
    OpenAnima::Core::RequestId m_turn{0};                       ///< Turn that may be stopped.
};

}  // namespace OpenAnimaTui::Application
