#include "OpenAnima/Core/NpcReply.h"

#include <string>

#include "OpenAnima/Errors.h"

namespace OpenAnima::Core {
namespace {

/// Reads a key that has to be there and has to be a string.
/// @param object Object to read it from.
/// @param key Name of the key.
/// @return The text of the key.
/// @throws ParseError when it is missing or is not a string.
std::string requiredString(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string()) {
        throw ParseError(std::string{"the reply has no string '"} + key + "': " + object.dump());
    }
    return found->get<std::string>();
}

}  // namespace

NpcReply readReply(const Completion& completion, const std::string& partialDialogue) {
    if (!completion.json.has_value()) {
        if (completion.finishReason == FinishReason::Cancelled) {
            NpcReply cut;
            cut.dialogue = partialDialogue;
            cut.completion = completion;
            return cut;
        }
        throw ParseError("the answer was asked for as a reply and has no JSON: " + completion.text);
    }

    const nlohmann::json& document = *completion.json;
    if (!document.is_object()) {
        throw ParseError("the reply is not a JSON object: " + document.dump());
    }

    NpcReply reply;
    reply.reasoning = requiredString(document, "reasoning");
    reply.dialogue = requiredString(document, "dialogue");

    const auto actions = document.find("actions");
    if (actions == document.end() || !actions->is_array()) {
        throw ParseError("the reply has no 'actions' array: " + document.dump());
    }
    for (const nlohmann::json& entry : *actions) {
        if (!entry.is_object()) {
            throw ParseError("an action is not a JSON object: " + entry.dump());
        }
        const auto arguments = entry.find("arguments");
        if (arguments == entry.end() || !arguments->is_object()) {
            throw ParseError("an action has no 'arguments' object: " + entry.dump());
        }
        reply.actions.push_back(NpcAction{requiredString(entry, "name"), *arguments});
    }

    reply.document = document;
    reply.completion = completion;
    return reply;
}

}  // namespace OpenAnima::Core
