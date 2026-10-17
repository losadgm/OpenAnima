#include "OpenAnima/Core/ActionCatalogue.h"

#include <cassert>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace OpenAnima::Core {
namespace {

/// What the model reads about the inner voice. Written once because it goes to two places:
/// the description of the schema, and the instruction that a provider ignoring the schema
/// still reads. Two copies would drift apart, and say different things about one field.
constexpr const char* ReasoningField =
    "Your private thoughts before you answer, in character and in the first person. One or two "
    "sentences. Nothing here is said out loud.";

/// What the model reads about the spoken part. Written once, as ReasoningField is.
constexpr const char* DialogueField =
    "What you say out loud, in character. An empty string when you say nothing.";

/// What the model reads about the actions. Written once, as ReasoningField is.
constexpr const char* ActionsField =
    "What you ask the host to do, as a list. Every entry is an object with \"name\" and "
    "\"arguments\". Empty when you do nothing.";

/// The arguments of an action that takes none.
/// @return A schema that accepts an object with nothing in it.
nlohmann::json emptyArguments() {
    return nlohmann::json{{"type", "object"},
                          {"properties", nlohmann::json::object()},
                          {"additionalProperties", false}};
}

}  // namespace

ActionCatalogue ActionCatalogue::base() {
    ActionCatalogue catalogue;
    catalogue.m_actions = {
        ActionSpec{"wait", "Do nothing this turn.", nlohmann::json{}},
        ActionSpec{"end_conversation", "End the conversation with the player.", nlohmann::json{}},
        ActionSpec{
            "emote", "Show a gesture or an expression, such as a nod or a shrug.",
            nlohmann::json{
                {"type", "object"},
                {"properties",
                 nlohmann::json{
                     {"kind", nlohmann::json{{"type", "string"},
                                             {"description", "The gesture or expression, such as a "
                                                             "nod or a shrug."}}}}},
                {"required", nlohmann::json::array({"kind"})},
                {"additionalProperties", false}}},
    };
    return catalogue;
}

void ActionCatalogue::add(ActionSpec spec) {
    assert(!spec.name.empty() && "an action needs a name");

    for (ActionSpec& existing : m_actions) {
        if (existing.name == spec.name) {
            existing = std::move(spec);
            return;
        }
    }
    m_actions.push_back(std::move(spec));
}

nlohmann::json ActionCatalogue::replySchema() const {
    nlohmann::json actions = nlohmann::json{{"type", "array"}, {"description", ActionsField}};
    if (m_actions.empty()) {
        // No action is allowed, and the schema says so without an empty "oneOf", which
        // is not a schema every provider accepts.
        actions["maxItems"] = 0;
    } else {
        nlohmann::json alternatives = nlohmann::json::array();
        for (const ActionSpec& action : m_actions) {
            const nlohmann::json arguments =
                action.arguments.is_object() ? action.arguments : emptyArguments();
            alternatives.push_back(nlohmann::json{
                {"type", "object"},
                {"properties", nlohmann::json{{"name", nlohmann::json{{"const", action.name}}},
                                              {"arguments", arguments}}},
                {"required", nlohmann::json::array({"name", "arguments"})},
                {"additionalProperties", false}});
        }
        actions["items"] = nlohmann::json{{"oneOf", std::move(alternatives)}};
    }

    return nlohmann::json{
        {"type", "object"},
        {"properties",
         nlohmann::json{
             {"reasoning", nlohmann::json{{"type", "string"}, {"description", ReasoningField}}},
             {"dialogue", nlohmann::json{{"type", "string"}, {"description", DialogueField}}},
             {"actions", std::move(actions)}}},
        {"required", nlohmann::json::array({"reasoning", "dialogue", "actions"})},
        {"additionalProperties", false}};
}

std::string ActionCatalogue::replyInstruction() const {
    std::string instruction =
        "Answer with one json object and nothing else, with these keys in this order:\n"
        "\n"
        "1. \"reasoning\": ";
    instruction += ReasoningField;
    instruction += "\n2. \"dialogue\": ";
    instruction += DialogueField;
    instruction += "\n3. \"actions\": ";
    instruction += ActionsField;
    instruction += '\n';

    if (m_actions.empty()) {
        instruction += "\nYou cannot take actions.\n";
        return instruction;
    }

    instruction += "\nThe actions you may take are:\n";
    for (const ActionSpec& action : m_actions) {
        instruction += "- " + action.name + ": " + action.description + "\n";
    }
    return instruction;
}

}  // namespace OpenAnima::Core
