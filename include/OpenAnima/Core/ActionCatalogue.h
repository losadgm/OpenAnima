/// @file
/// @brief The actions an NPC may take, and the reply schema they generate.

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace OpenAnima::Core {

/// One action an NPC may ask the host application to take.
struct ActionSpec {
    std::string name;          ///< Name the model writes. Unique within a catalogue.
    std::string description;   ///< What it does, as the model reads it.
    nlohmann::json arguments;  ///< JSON Schema of its arguments. Empty for an action with none.
};

/// The actions available to one NPC, and the reply schema built from them.
///
/// One definition serves two things: the schema the answer is asked for in, and the
/// instruction sent with the prompt, which is what a provider that ignores the schema
/// still reads.
class ActionCatalogue {
public:
    /// @return The catalogue the reply schema describes: wait, end_conversation and emote.
    [[nodiscard]] static ActionCatalogue base();

    /// Adds an action, or replaces the one that already has its name.
    /// @param spec Action to add. Its name must not be empty.
    void add(ActionSpec spec);

    /// @return The actions, in the order they were added.
    [[nodiscard]] const std::vector<ActionSpec>& actions() const noexcept {
        return m_actions;
    }

    /// @return The schema a reply is asked for in: reasoning, dialogue and the actions
    ///         of this catalogue, with every key required.
    [[nodiscard]] nlohmann::json replySchema() const;

    /// @return The instruction that goes with the prompt: what to answer with, in which
    ///         order, and which actions exist. It asks for the keys in the order that
    ///         streams best, because a schema does not carry an order.
    [[nodiscard]] std::string replyInstruction() const;

private:
    std::vector<ActionSpec> m_actions;  ///< Actions the NPC may take.
};

}  // namespace OpenAnima::Core
