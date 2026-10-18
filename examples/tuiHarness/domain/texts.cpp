#include "domain/texts.h"

#include <cstddef>
#include <initializer_list>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "domain/TuiTextsData.h"

namespace OpenAnimaTui::Domain {

namespace {

/// @param document The texts.
/// @param key Name of the text.
/// @return The text.
std::string text(const nlohmann::json& document, std::string_view key) {
    return document.at(std::string{key}).get<std::string>();
}

/// Reads the lines of a list of texts.
/// @param document The texts.
/// @param key Name of the list.
/// @return The lines, in order.
std::vector<std::string> lines(const nlohmann::json& document, std::string_view key) {
    return document.at(std::string{key}).get<std::vector<std::string>>();
}

}  // namespace

const Texts& texts() {
    static const Texts read = [] {
        const nlohmann::json document = nlohmann::json::parse(TuiTextsData::Json);
        return Texts{
            text(document, "waitForTurn"),
            lines(document, "help"),
            text(document, "createdNpc"),
            text(document, "noSuchNpc"),
            text(document, "noNpcs"),
            text(document, "npcListTitle"),
            text(document, "npcLine"),
            text(document, "npcHere"),
            text(document, "createUsage"),
            text(document, "enterUsage"),
            text(document, "leftConversation"),
            text(document, "noNpcToClear"),
            text(document, "conversationDropped"),
            text(document, "rawOn"),
            text(document, "rawOff"),
            text(document, "rawUsage"),
            text(document, "noNpc"),
            text(document, "turnFailed"),
            text(document, "cancelled"),
            text(document, "noDocument"),
            text(document, "npcName"),
            text(document, "npcNameLobby"),
            text(document, "speakerYou"),
            text(document, "placeholderLobby"),
            text(document, "placeholderActive"),
            text(document, "statusBusy"),
            text(document, "statusCancelTail"),
            text(document, "statusCancellingTail"),
            text(document, "statusHintActive"),
            text(document, "statusHintLobby"),
            text(document, "rawLabel"),
        };
    }();
    return read;
}

std::string fill(std::string_view pattern,
                 std::initializer_list<std::pair<std::string_view, std::string>> values) {
    std::string text{pattern};
    for (const auto& [name, value] : values) {
        const std::string placeholder = "{" + std::string{name} + "}";
        const std::size_t pos = text.find(placeholder);
        if (pos != std::string::npos) {
            text.replace(pos, placeholder.size(), value);
        }
    }
    return text;
}

}  // namespace OpenAnimaTui::Domain
