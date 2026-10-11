#pragma once

#include <memory>
#include <string>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Provider/IProvider.h"

namespace OpenAnima::Core {

/// Entry point of the cognitive middleware.
/// Owns the conversation state of a single agent and exposes it to the client application.
class NPCBrain {
public:
    /// Creates a brain without a backend. Attach one with setProvider().
    NPCBrain() = default;

    /// Creates a brain bound to a backend.
    /// @param provider Backend used for every request.
    explicit NPCBrain(std::shared_ptr<OpenAnima::Provider::IProvider> provider);

    /// Sets the backend used for later requests.
    /// @param provider Backend used for every request.
    void setProvider(std::shared_ptr<OpenAnima::Provider::IProvider> provider);

    /// Sets the persona or instruction that leads every request.
    /// The prompt is resolved on each call and never becomes part of the stored history.
    /// @param prompt Instruction sent as the leading system message.
    void setSystemPrompt(std::string prompt);

    /// Sends a prompt and returns the answer, with what the call produced.
    /// The prompt and the answer text join the history only when the call succeeds.
    /// @param prompt Text produced by the player or by the client application.
    /// @return The assistant answer, and why generation stopped.
    /// @throws OpenAnimaError when no backend is attached or the call fails.
    Completion ask(const std::string& prompt);

    /// @return The conversation so far, in chronological order.
    const std::vector<ChatMessage>& history() const noexcept {
        return m_history;
    }

    /// Drops the conversation, keeping the backend and the system prompt.
    void clearHistory();

    /// Verifies that the library is linked and operational.
    /// @return A human-readable status message.
    std::string ping();

private:
    std::vector<ChatMessage> m_history;  ///< Conversation context owned by this brain.
    std::shared_ptr<OpenAnima::Provider::IProvider> m_provider;  ///< Backend for every request.
    std::string m_systemPrompt;  ///< Instruction leading every request.
};

}  // namespace OpenAnima::Core
