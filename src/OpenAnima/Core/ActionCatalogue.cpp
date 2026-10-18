#include "OpenAnima/Core/ActionCatalogue.h"

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "OpenAnima/Core/PromptsData.h"

namespace OpenAnima::Core {
namespace {

/// The words the contract is asked in, read once from the json the build embedded.
/// @return The whole document.
const nlohmann::json& prompts() {
    static const nlohmann::json document = nlohmann::json::parse(PromptsData::Json);
    return document;
}

/// @param document The prompts.
/// @param key Name of the text.
/// @return The text.
std::string prompt(std::string_view key) {
    return prompts().at(std::string{key}).get<std::string>();
}

/// Replaces one placeholder in a text, leaving it alone when it is not there.
/// @param text Text to change.
/// @param name Name of the placeholder, without its braces.
/// @param value What replaces it.
void replaceOnce(std::string& text, std::string_view name, const std::string& value) {
    const std::string placeholder = "{" + std::string{name} + "}";
    const std::size_t pos = text.find(placeholder);
    if (pos != std::string::npos) {
        text.replace(pos, placeholder.size(), value);
    }
}

/// Fills the placeholders of a text.
/// @param pattern Text with placeholders such as `{name}`.
/// @param values Each placeholder and what replaces it.
/// @return The text with every placeholder replaced.
std::string fill(std::string_view pattern,
                 std::initializer_list<std::pair<std::string_view, std::string>> values) {
    std::string text{pattern};
    for (const auto& [name, value] : values) {
        replaceOnce(text, name, value);
    }
    return text;
}

/// The arguments of an action that takes none.
/// @return A schema that accepts an object with nothing in it.
nlohmann::json emptyArguments() {
    return nlohmann::json{{"type", "object"},
                          {"properties", nlohmann::json::object()},
                          {"additionalProperties", false}};
}

}  // namespace

ActionCatalogue ActionCatalogue::base() {
    const nlohmann::json& words = prompts().at("catalogue");

    ActionCatalogue catalogue;
    catalogue.m_actions = {
        ActionSpec{"wait", words.at("wait").at("description"), nlohmann::json{}},
        ActionSpec{"end_conversation", words.at("end_conversation").at("description"),
                   nlohmann::json{}},
        ActionSpec{"emote", words.at("emote").at("description"),
                   nlohmann::json{
                       {"type", "object"},
                       {"properties",
                        nlohmann::json{{"kind", nlohmann::json{{"type", "string"},
                                                               {"description",
                                                                words.at("emote").at("kind")}}}}},
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
    const nlohmann::json& words = prompts();

    nlohmann::json actions =
        nlohmann::json{{"type", "array"}, {"description", words.at("actions")}};
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
         nlohmann::json{{"reasoning", nlohmann::json{{"type", "string"},
                                                     {"description", words.at("reasoning")}}},
                        {"dialogue",
                         nlohmann::json{{"type", "string"}, {"description", words.at("dialogue")}}},
                        {"actions", std::move(actions)}}},
        {"required", nlohmann::json::array({"reasoning", "dialogue", "actions"})},
        {"additionalProperties", false}};
}

std::string ActionCatalogue::replyInstruction() const {
    std::string instruction = fill(prompt("instruction"), {{"reasoning", prompt("reasoning")},
                                                           {"dialogue", prompt("dialogue")},
                                                           {"actions", prompt("actions")}});

    if (m_actions.empty()) {
        instruction += prompt("noActions");
        return instruction;
    }

    instruction += prompt("actionsIntro");
    for (const ActionSpec& action : m_actions) {
        instruction += fill(prompt("actionLine"),
                            {{"name", action.name}, {"description", action.description}});
    }
    return instruction;
}

}  // namespace OpenAnima::Core
