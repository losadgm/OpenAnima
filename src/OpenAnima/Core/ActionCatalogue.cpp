#include "OpenAnima/Core/ActionCatalogue.h"

#include <cassert>
#include <cstddef>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>

#include "OpenAnima/Core/PromptsData.h"
#include "OpenAnima/Core/ReplySchemaData.h"

namespace OpenAnima::Core {
namespace {

/// The words the contract is asked in, read once from the json the build embedded.
/// @return The whole document.
const nlohmann::json& prompts() {
    static const nlohmann::json document = nlohmann::json::parse(PromptsData::Json);
    return document;
}

/// The reply contract, read once from the json the build embedded.
/// @return The whole document.
const nlohmann::json& schemaData() {
    static const nlohmann::json document = nlohmann::json::parse(ReplySchemaData::Json);
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

/// One entry of the reply schema's list of actions.
/// @param spec Action to describe.
/// @return The alternative the model picks by name.
nlohmann::json alternative(const ActionSpec& spec) {
    const nlohmann::json arguments = spec.arguments.is_object() ? spec.arguments : emptyArguments();
    return nlohmann::json{
        {"type", "object"},
        {"description", spec.description},
        {"properties",
         nlohmann::json{{"name", nlohmann::json{{"const", spec.name}}}, {"arguments", arguments}}},
        {"required", nlohmann::json::array({"name", "arguments"})},
        {"additionalProperties", false}};
}

}  // namespace

ActionCatalogue ActionCatalogue::base() {
    ActionCatalogue catalogue;
    for (const nlohmann::json& entry :
         schemaData().at("properties").at("actions").at("items").at("oneOf")) {
        const nlohmann::json& arguments = entry.at("properties").at("arguments");

        ActionSpec spec;
        spec.name = entry.at("properties").at("name").at("const").get<std::string>();
        spec.description = entry.at("description").get<std::string>();
        // An action with nothing to pass leaves the field empty, and the schema built from
        // the specification then closes its arguments on an empty object.
        if (arguments.contains("properties") && !arguments.at("properties").empty()) {
            spec.arguments = arguments;
        }
        catalogue.add(std::move(spec));
    }
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
    nlohmann::json schema = schemaData();
    nlohmann::json& actions = schema.at("properties").at("actions");

    if (m_actions.empty()) {
        // No action is allowed, and the schema says so without an empty "oneOf", which
        // is not a schema every provider accepts.
        actions.erase("items");
        actions["maxItems"] = 0;
        return schema;
    }

    nlohmann::json alternatives = nlohmann::json::array();
    for (const ActionSpec& action : m_actions) {
        alternatives.push_back(alternative(action));
    }
    actions["items"] = nlohmann::json{{"oneOf", std::move(alternatives)}};
    return schema;
}

std::string ActionCatalogue::replyInstruction() const {
    const nlohmann::json& properties = schemaData().at("properties");

    std::string instruction =
        fill(prompt("instruction"),
             {{"reasoning", properties.at("reasoning").at("description").get<std::string>()},
              {"dialogue", properties.at("dialogue").at("description").get<std::string>()},
              {"actions", properties.at("actions").at("description").get<std::string>()}});

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
