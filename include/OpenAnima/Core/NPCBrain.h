/// @file
/// @brief The agent that owns a conversation and talks to a provider.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Provider/IProvider.h"

namespace OpenAnima::Core {

/// One NPC: its persona, its memory and the conversation it is having.
class NPCBrain {
public:
    /// Creates a brain without a backend. Attach one with setProvider().
    NPCBrain() = default;

    /// Creates a brain bound to a backend.
    /// @param provider Backend used for every request.
    explicit NPCBrain(std::shared_ptr<OpenAnima::Provider::IProvider> provider);

    /// Destroys the brain and the history it holds.
    ~NPCBrain() = default;

    /// A brain is moved, not copied: copying would fork the history while sharing the
    /// backend with the copy.
    NPCBrain(const NPCBrain&) = delete;
    NPCBrain& operator=(const NPCBrain&) = delete;

    /// Moves a brain: the history and the backend move with it.
    NPCBrain(NPCBrain&&) noexcept = default;

    /// Move assignment.
    /// @return This brain.
    NPCBrain& operator=(NPCBrain&&) noexcept = default;

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

    /// Sends a prompt and reports the answer while it is being produced.
    /// The history is touched the same way ask() touches it, and only once the call
    /// has succeeded, so a caller that drew half an answer and then failed still has
    /// the conversation it had before.
    /// @param prompt Text produced by the player or by the client application.
    /// @param onDelta Called with every piece of the answer as it arrives, in order.
    /// @return The assembled answer, and why generation stopped.
    /// @throws OpenAnimaError when no backend is attached or the call fails.
    Completion ask(const std::string& prompt, const Provider::DeltaSink& onDelta);

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
