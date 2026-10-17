#include "cliHarness/session.h"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace OpenAnimaCli {
namespace {

/// The most characters of a persona the prompt shows.
constexpr std::size_t MaxPersonaInPrompt = 24;

/// Trims the spaces at both ends of a line.
/// @param text Text to trim.
/// @return The text without leading or trailing blanks.
std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return std::string{};
    }
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// @return The first line of a persona, cut when it is too long for the prompt.
std::string shortName(const std::string& persona) {
    std::string text = trim(persona.substr(0, persona.find('\n')));
    if (text.size() > MaxPersonaInPrompt) {
        text = text.substr(0, MaxPersonaInPrompt - 3) + "...";
    }
    return text;
}

/// @return How a command is written, as the help lists it.
std::string usageOf(const std::string& name) {
    for (const CommandHelp& command : commandHelp()) {
        if (command.usage.rfind(name, 0) == 0) {
            return command.usage;
        }
    }
    return name;
}

/// Reads the id out of a command argument.
/// @param text What was typed after the command.
/// @return The id, or nothing when the text is not a plain number.
std::optional<std::size_t> readId(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        return std::nullopt;
    }
    try {
        return static_cast<std::size_t>(std::stoull(text));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

/// @return The notes that list every command with what it does.
std::vector<std::string> helpNotes() {
    std::size_t widest = 0;
    for (const CommandHelp& command : commandHelp()) {
        widest = std::max(widest, command.usage.size());
    }

    std::vector<std::string> notes;
    notes.reserve(commandHelp().size());
    for (const CommandHelp& command : commandHelp()) {
        notes.push_back("  " + command.usage + std::string(widest - command.usage.size() + 2, ' ') +
                        command.description);
    }
    return notes;
}

}  // namespace

const std::vector<CommandHelp>& commandHelp() {
    // The order is the order the help lists them in: the ones that start a conversation
    // first, and the way out last.
    static const std::vector<CommandHelp> list = {
        {"/create <description>", "make an npc and talk to it"},
        {"/enter <id>", "talk to another npc"},
        {"/npcs", "list the npcs"},
        {"/leave", "leave the conversation"},
        {"/clear", "drop the active conversation, keep the persona"},
        {"/raw [on|off]", "print the raw JSON of every turn, or stop printing it"},
        {"/help", "this list"},
        {"/quit", "leave, the same as ctrl-d"},
    };
    return list;
}

std::size_t Session::create(const std::string& persona) {
    const std::size_t npcId = m_nextId;
    ++m_nextId;
    m_personas.emplace(npcId, trim(persona));
    m_active = npcId;
    return npcId;
}

Step Session::interpret(const std::string& line) {
    const std::string text = trim(line);
    if (text.empty()) {
        return Step{};
    }

    if (text[0] != '/') {
        Step step;
        if (!m_active.has_value()) {
            step.notes.emplace_back("no npc   /create to make one, /enter to pick one");
            return step;
        }
        step.order = Order{OrderKind::Ask, *m_active, text};
        return step;
    }

    const std::size_t space = text.find(' ');
    const std::string name = text.substr(0, space);
    const std::string argument =
        space == std::string::npos ? std::string{} : trim(text.substr(space));

    if (name == "/create") {
        return createCommand(argument);
    }
    if (name == "/enter") {
        return enterCommand(argument);
    }
    if (name == "/npcs") {
        return listCommand(argument);
    }
    if (name == "/leave") {
        return leaveCommand(argument);
    }
    if (name == "/clear") {
        return clearCommand(argument);
    }
    if (name == "/raw") {
        return rawCommand(argument);
    }
    if (name == "/help") {
        return helpCommand(argument);
    }
    if (name == "/quit") {
        if (!argument.empty()) {
            return Step{{}, {"usage: " + usageOf("/quit")}, false};
        }
        Step step;
        step.quit = true;
        return step;
    }

    Step step;
    step.notes.push_back("unknown command '" + name + "'   /help to list the commands");
    return step;
}

std::string Session::prompt() const {
    if (!m_active.has_value()) {
        return "[lobby] > ";
    }
    const auto persona = m_personas.find(*m_active);
    const std::string name =
        persona == m_personas.end() ? std::string{} : shortName(persona->second);
    return "[" + std::to_string(*m_active) + " " + name + "] > ";
}

Step Session::createCommand(const std::string& argument) {
    if (argument.empty()) {
        return Step{{}, {"usage: " + usageOf("/create")}, false};
    }
    const std::size_t npcId = create(argument);
    Step step;
    step.notes.push_back("npc " + std::to_string(npcId) + " created   talking to it");
    step.order = Order{OrderKind::CreateNpc, npcId, argument};
    return step;
}

Step Session::enterCommand(const std::string& argument) {
    if (argument.empty()) {
        return Step{{}, {"usage: " + usageOf("/enter")}, false};
    }
    const std::optional<std::size_t> npcId = readId(argument);
    if (!npcId.has_value()) {
        return Step{{}, {"usage: " + usageOf("/enter") + ", a number from /npcs"}, false};
    }
    if (m_personas.find(*npcId) == m_personas.end()) {
        return Step{{}, {"no such npc   /npcs to list them"}, false};
    }
    m_active = npcId;
    return Step{{}, {"talking to npc " + std::to_string(*npcId)}, false};
}

Step Session::listCommand(const std::string& argument) {
    if (!argument.empty()) {
        return Step{{}, {"usage: " + usageOf("/npcs")}, false};
    }
    if (m_personas.empty()) {
        return Step{{}, {"no npcs yet   /create to make one"}, false};
    }

    Step step;
    for (const auto& entry : m_personas) {
        const bool current = m_active.has_value() && *m_active == entry.first;
        step.notes.push_back(std::string{current ? "* " : "  "} + std::to_string(entry.first) +
                             "  " + entry.second);
    }
    return step;
}

Step Session::leaveCommand(const std::string& argument) {
    if (!argument.empty()) {
        return Step{{}, {"usage: " + usageOf("/leave")}, false};
    }
    if (!m_active.has_value()) {
        return Step{{}, {"already in the lobby   /create to make one, /enter to pick one"}, false};
    }
    m_active.reset();
    return Step{{}, {"in the lobby   /enter to pick an npc again"}, false};
}

Step Session::clearCommand(const std::string& argument) {
    if (!argument.empty()) {
        return Step{{}, {"usage: " + usageOf("/clear")}, false};
    }
    if (!m_active.has_value()) {
        return Step{{}, {"no npc   /create to make one, /enter to pick one"}, false};
    }
    Step step;
    step.notes.emplace_back("conversation cleared, persona kept");
    step.order = Order{OrderKind::Clear, *m_active, std::string{}};
    return step;
}

Step Session::rawCommand(const std::string& argument) {
    if (!argument.empty() && argument != "on" && argument != "off") {
        return Step{{}, {"usage: " + usageOf("/raw")}, false};
    }
    if (!argument.empty()) {
        m_raw = argument == "on";
    } else {
        m_raw = !m_raw;
    }
    return Step{{}, {std::string{"raw json "} + (m_raw ? "on" : "off")}, false};
}

Step Session::helpCommand(const std::string& argument) {
    if (!argument.empty()) {
        return Step{{}, {"usage: " + usageOf("/help")}, false};
    }
    return Step{{}, helpNotes(), false};
}

}  // namespace OpenAnimaCli
