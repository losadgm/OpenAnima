/// @file
/// @brief What happened to a turn that was asked for asynchronously.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/NpcReply.h"

namespace OpenAnima::Core {

/// Names a turn that was handed to a brain and not collected yet.
using RequestId = std::uint64_t;

/// What an event reports, in the order the events of one turn arrive.
enum class EventKind : std::uint8_t {
    Started,   ///< The turn left the queue and the backend is working on it.
    Delta,     ///< A piece of the answer. The whole answer is not ready yet.
    Finished,  ///< The turn is over and `completion` holds the answer.
    Failed     ///< The turn could not be made and `error` says why.
};

/// One thing that happened to one turn.
///
/// Only the fields of its kind carry something: `text` for a Delta, `completion` and
/// `reply` for a Finished and `error` for a Failed. A Delta of a turn that asked for a
/// reply carries the dialogue, not the JSON that is being written.
struct Event {
    RequestId id{};                 ///< Turn the event belongs to.
    EventKind kind{};               ///< What happened.
    std::string text;               ///< Piece of the answer, for a Delta.
    Completion completion;          ///< Answer and why it ended, for a Finished.
    std::optional<NpcReply> reply;  ///< Reply, for a Finished turn that asked for one.
    std::string error;              ///< What went wrong, for a Failed.
};

}  // namespace OpenAnima::Core
