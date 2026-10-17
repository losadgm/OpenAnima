/// @file
/// @brief The vocabulary of the prompt: the commands, and what completes them.

#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace OpenAnimaTui::Domain {

/// The commands the prompt completes, in the order the hints show them.
/// @return The commands.
const std::vector<std::string>& commands();

/// The commands that start with what has been typed so far.
/// @param draft The draft.
/// @param cursor Where the caret is, in characters.
/// @return The matches, empty when the caret is not inside a command.
std::vector<std::string> commandMatches(const std::string& draft, std::size_t cursor);

/// Replaces what is typed with the chosen command, ready for its argument.
/// @param draft The draft.
/// @param cursor Where the caret is, in characters.
/// @param chosen The command to complete with.
/// @return The new draft and the new cursor.
std::pair<std::string, std::size_t> completeCommand(const std::string& draft, std::size_t cursor,
                                                    const std::string& chosen);

}  // namespace OpenAnimaTui::Domain
