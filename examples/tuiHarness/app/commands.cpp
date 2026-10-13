#include "commands.h"

#include <algorithm>

namespace OpenAnimaTui::App {

const std::vector<std::string>& commands() {
    static const std::vector<std::string> list = {
        "/create", "/enter", "/npcs", "/leave", "/help", "/history clear", "/quit",
    };
    return list;
}

/// The commands that complete what is being typed: those that start with the leading word.
///
/// An exact match is left out. There is nothing to complete once the word is the command, and
/// holding on to it would keep the arrows on the hints instead of on the history.
///
/// @param draft Draft as typed.
/// @param cursor Caret position in bytes.
/// @return The matching commands, in the order they are offered.
std::vector<std::string> commandMatches(const std::string& draft, std::size_t cursor) {
    std::vector<std::string> matches;
    const std::string typed = draft.substr(0, std::min(cursor, draft.size()));
    if (typed.empty() || typed[0] != '/' || typed.find(' ') != std::string::npos) {
        return matches;
    }
    for (const std::string& command : commands()) {
        if (command.rfind(typed, 0) == 0 && command != typed) {
            matches.push_back(command);
        }
    }
    return matches;
}

std::pair<std::string, std::size_t> completeCommand(const std::string& draft, std::size_t cursor,
                                                    const std::string& chosen) {
    const std::size_t caret = std::min(cursor, draft.size());
    const std::string completed = chosen + " ";
    return {completed + draft.substr(caret), completed.size()};
}

}  // namespace OpenAnimaTui::App
