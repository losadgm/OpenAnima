/// @file
/// @brief The conversation the client is having: the npcs, who is active, and what a line
/// asks for.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace OpenAnimaCli {

/// One command of the prompt: how it is written and what it does.
struct CommandHelp {
    std::string usage;        ///< For example "/enter <id>".
    std::string description;  ///< What it does, in one line.
};

/// @return The commands, in the order the help lists them.
const std::vector<CommandHelp>& commandHelp();

/// What the shell has to do with a brain, because the client only holds the text.
enum class OrderKind : std::uint8_t {
    Nothing,    ///< Nothing to do.
    CreateNpc,  ///< Make a brain for the npc that has just been created.
    Clear,      ///< Drop the conversation of the active npc.
    Ask         ///< Send text to the active npc.
};

/// One piece of work for the shell.
struct Order {
    OrderKind kind{OrderKind::Nothing};
    std::size_t npcId{0};  ///< The npc the order is about.
    std::string text;      ///< What to send, for an Ask.
};

/// What one line produced: the work, and what to say about it.
struct Step {
    Order order;                     ///< What the shell has to do.
    std::vector<std::string> notes;  ///< Lines to print, in order.
    bool quit{false};                ///< Whether the client should leave.
};

/// The npcs the client knows about and the one it is talking to.
class Session {
public:
    /// Creates an npc and talks to it.
    /// @param persona What the npc is.
    /// @return The id the npc was given.
    std::size_t create(const std::string& persona);

    /// Reads one line: a command when it starts with a slash, a prompt otherwise.
    /// @param line What was typed.
    /// @return The work the line asks for, and the notes that go with it.
    Step interpret(const std::string& line);

    /// @return The prompt that asks for the next line, naming the active npc.
    [[nodiscard]] std::string prompt() const;

    /// @return Whether every turn is printed as raw JSON.
    [[nodiscard]] bool raw() const noexcept {
        return m_raw;
    }

    /// Sets whether every turn is printed as raw JSON.
    /// @param raw Whether to print it.
    void setRaw(bool raw) noexcept {
        m_raw = raw;
    }

    /// @return The npc being talked to, when there is one.
    [[nodiscard]] std::optional<std::size_t> active() const noexcept {
        return m_active;
    }

private:
    /// Handles "/create".
    Step createCommand(const std::string& argument);
    /// Handles "/enter".
    Step enterCommand(const std::string& argument);
    /// Handles "/npcs".
    Step listCommand(const std::string& argument);
    /// Handles "/leave".
    Step leaveCommand(const std::string& argument);
    /// Handles "/clear".
    Step clearCommand(const std::string& argument);
    /// Handles "/raw".
    Step rawCommand(const std::string& argument);
    /// Handles "/help".
    static Step helpCommand(const std::string& argument);

    std::map<std::size_t, std::string> m_personas;  ///< What each npc is, by id.
    std::size_t m_nextId{1};                        ///< Id the next npc will get.
    std::optional<std::size_t> m_active;            ///< Npc being talked to.
    bool m_raw{false};                              ///< Whether turns are printed raw.
};

}  // namespace OpenAnimaCli
