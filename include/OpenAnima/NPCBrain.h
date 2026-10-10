#pragma once

#include <string>
#include <vector>

#include "OpenAnima/Types.h"

namespace OpenAnima {

/// Entry point of the cognitive middleware.
/// Owns the reasoning state of a single agent and exposes it to the client application.
class NPCBrain {
public:
    /// Creates an idle brain with an empty conversation context.
    NPCBrain() = default;

    /// Verifies that the library is linked and operational.
    /// @return A human-readable status message.
    std::string ping();

private:
    std::vector<ChatMessage> m_history;  ///< Conversation context owned by this brain.
};

}  // namespace OpenAnima
