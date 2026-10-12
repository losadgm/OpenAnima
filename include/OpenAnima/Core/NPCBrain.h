/// @file
/// @brief The agent that owns a conversation and talks to a provider.

#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Event.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Provider/IProvider.h"

namespace OpenAnima::Core {

/// One NPC: its persona, its memory and the conversation it is having.
class NPCBrain {
public:
    /// Creates a brain without a backend. Attach one with setProvider().
    NPCBrain();

    /// Creates a brain bound to a backend.
    /// @param provider Backend used for every request.
    explicit NPCBrain(std::shared_ptr<OpenAnima::Provider::IProvider> provider);

    /// Destroys the brain and the history it holds, stopping a turn in flight.
    ~NPCBrain();

    /// A brain is moved, not copied: copying would fork the history while sharing the
    /// backend with the copy.
    NPCBrain(const NPCBrain&) = delete;
    NPCBrain& operator=(const NPCBrain&) = delete;

    /// Moves a brain: the history and the backend move with it. A turn in flight
    /// keeps running and reports to the brain it was asked from.
    NPCBrain(NPCBrain&&) noexcept;

    /// Move assignment.
    /// @return This brain.
    NPCBrain& operator=(NPCBrain&&) noexcept;

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
    /// @param call Where the pieces of the answer go, and how the call can be
    ///        abandoned half way.
    /// @return The assembled answer, and why generation stopped.
    /// @throws OpenAnimaError when no backend is attached or the call fails.
    Completion ask(const std::string& prompt, const Provider::Call& call);

    /// Sends a prompt and reports the answer while it is being produced.
    /// @param prompt Text produced by the player or by the client application.
    /// @param onDelta Called with every piece of the answer as it arrives, in order.
    /// @return The assembled answer, and why generation stopped.
    /// @throws OpenAnimaError when no backend is attached or the call fails.
    Completion ask(const std::string& prompt, const Provider::DeltaSink& onDelta) {
        return ask(prompt, Provider::Call{onDelta, {}});
    }

    /// Asks for a turn and returns at once, without waiting for the answer.
    ///
    /// The turn reports itself as events; see poll(). The conversation is not
    /// touched until the last event of the turn has been collected, so a client
    /// that draws the answer as it arrives writes the turn down once it is done
    /// drawing it.
    /// @param prompt Text produced by the player or by the client application.
    /// @return The id that names this turn in the events that follow.
    /// @throws OpenAnimaError when no backend is attached, or when a turn is
    ///         already in flight on this brain.
    RequestId askAsync(std::string prompt);

    /// Asks a turn in flight to stop before the answer is complete.
    ///
    /// What the backend had produced by then arrives as the turn's Finished
    /// event, and the answer is marked as cancelled. An id that is not the turn
    /// in flight is ignored, so a client can call this with an id it collected
    /// long ago without checking it first.
    /// @param id Turn to stop, as returned by askAsync().
    void cancel(RequestId id);

    /// Collects the next event of a turn, waiting for one to appear.
    ///
    /// Call it until it hands back a Finished or a Failed event: that is what
    /// ends the turn, writes it into the history and frees the brain for the
    /// next one.
    /// @param timeout Longest wait for an event. Zero collects what is already
    ///        waiting and returns at once.
    /// @return The event, or nothing when none arrived in time.
    std::optional<Event> poll(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    /// @return Whether a turn has been asked for and not collected yet.
    bool isBusy() const noexcept;

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
    /// The thread, the queue and the events of the async calls. Kept behind a
    /// pointer so that the brain stays movable while a turn is in flight, and so
    /// that threads stay out of this header.
    class TurnWorker;

    std::vector<ChatMessage> m_history;  ///< Conversation context owned by this brain.
    std::shared_ptr<OpenAnima::Provider::IProvider> m_provider;  ///< Backend for every request.
    std::string m_systemPrompt;            ///< Instruction leading every request.
    std::unique_ptr<TurnWorker> m_worker;  ///< Runs the async turns, created on first use.
    std::string m_pendingPrompt;  ///< Prompt of the turn in flight, waiting for its answer.
};

}  // namespace OpenAnima::Core
