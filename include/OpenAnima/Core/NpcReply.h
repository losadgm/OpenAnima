/// @file
/// @brief What an NPC answered: what it thought, what it says and what it does.

#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "OpenAnima/Core/Completion.h"

namespace OpenAnima::Core {

/// One thing an NPC asks the host application to do.
struct NpcAction {
    std::string name;                  ///< Action name, as the catalogue declared it.
    nlohmann::ordered_json arguments;  ///< Arguments the model filled in, as an object.
};

/// The answer of a turn, split into the three variables the reply contract defines.
///
/// `reasoning` is the NPC's inner voice: what it thinks before it speaks, in character
/// and in the first person. The library never reads into it. It hands it over, and keeps
/// the whole document in the history so the NPC reads it again on the next turn.
struct NpcReply {
    std::string reasoning;            ///< Private thoughts. Empty when it had none.
    std::string dialogue;             ///< What it says out loud. Empty when it says nothing.
    std::vector<NpcAction> actions;   ///< What it asks the host to do.
    nlohmann::ordered_json document;  ///< The answer as it arrived, in its own order.
    Completion completion;            ///< The answer as the provider reported it.
};

/// Reads a reply out of an answer asked for with a reply schema.
/// @param completion Answer as the provider reported it.
/// @param partialDialogue Dialogue read from the answer while it arrived, used when the
///        turn was cancelled before the document was whole.
/// @return The reply, with the document and the provider metadata.
/// @throws ParseError when the answer is not a reply, or a key is missing or has the
///         wrong type. A cancelled turn is not an error: it returns what it had said.
NpcReply readReply(const Completion& completion, const std::string& partialDialogue = {});

}  // namespace OpenAnima::Core
