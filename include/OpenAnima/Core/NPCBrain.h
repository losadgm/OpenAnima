/// @file
/// @brief The agent that owns a conversation and talks to a provider.

#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "OpenAnima/Core/ActionCatalogue.h"
#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Event.h"
#include "OpenAnima/Core/NpcReply.h"
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

    /// Sets the actions this NPC may take, and with them the shape of its answers.
    /// The base catalogue is used until this is called.
    /// @param catalogue Actions the NPC may take.
    void setReplyCatalogue(ActionCatalogue catalogue);

    /// @return The actions this NPC may take.
    [[nodiscard]] const ActionCatalogue& replyCatalogue() const noexcept {
        return m_catalogue;
    }

    /// Sends a prompt and returns the answer, with what the call produced.
    /// The prompt and the answer text join the history only when the call succeeds.
    /// @param prompt Text produced by the player or by the client application.
    /// @return The assistant answer, and why generation stopped.
    /// @throws OpenAnimaError when no backend is attached or the call fails.
    Completion ask(const std::string& prompt);

    /// Sends a prompt and reports the answer while it is being produced.
    /// The history is touched the same way ask() touches it, and only once the call
    /// has succeeded and was not cancelled, so a caller that drew half an answer and
    /// then failed or stopped still has the conversation it had before.
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
        return ask(prompt, Provider::Call{onDelta, {}, {}});
    }

    /// Asks for a turn as a reply: what the NPC thought, what it says and what it does.
    ///
    /// The answer is asked for as JSON, following the schema built from the catalogue,
    /// and read back from there. The contract also travels in the instruction that goes
    /// with the prompt, so a provider that ignores the schema still answers in it. The
    /// whole document joins the history as the assistant turn, which is what lets the
    /// NPC read its own thoughts on the next one.
    /// @param prompt Text produced by the player or by the client application.
    /// @return The reply, with the document and the provider metadata.
    /// @throws OpenAnimaError when no backend is attached, the call fails, or the answer
    ///         is not the reply that was asked for.
    NpcReply askTurn(const std::string& prompt);

    /// Asks for a turn as a reply and reports the dialogue while it is written.
    /// @param prompt Text produced by the player or by the client application.
    /// @param onDialogue Called with the dialogue as it arrives. Reasoning is not
    ///        streamed: it comes with the reply, once the turn is over.
    /// @return The reply, with the document and the provider metadata.
    /// @throws OpenAnimaError when no backend is attached, the call fails, or the answer
    ///         is not the reply that was asked for.
    NpcReply askTurn(const std::string& prompt, const Provider::DeltaSink& onDialogue);

    /// Asks for a reply turn and returns at once, without waiting for the answer.
    ///
    /// Its events are the ones of askAsync(), with one difference: a Delta carries the
    /// dialogue as it is written, not the JSON that is being written, and the Finished
    /// event carries the reply. A cancelled turn leaves the history as it was, as a raw
    /// turn does.
    /// @param prompt Text produced by the player or by the client application.
    /// @return The id that names this turn in the events that follow.
    /// @throws OpenAnimaError when no backend is attached, or when a turn is
    ///         already in flight on this brain.
    RequestId askTurnAsync(std::string prompt);

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
    /// event, and the answer is marked as cancelled. The turn is not written into
    /// the conversation, whatever kind of turn it was: only one that ran to the
    /// end is. An id that is not the turn in flight is ignored, so a client can
    /// call this with an id it collected long ago without checking it first.
    /// @param turnId Turn to stop, as returned by askAsync().
    void cancel(RequestId turnId);

    /// Collects the next event of a turn, waiting for one to appear.
    ///
    /// Call it until it hands back a Finished or a Failed event: that is what
    /// ends the turn and frees the brain for the next one. A turn that ran to the
    /// end is then written into the history; one that failed or was cancelled
    /// leaves it as it was.
    /// @param timeout Longest wait for an event. Zero collects what is already
    ///        waiting and returns at once.
    /// @return The event, or nothing when none arrived in time.
    std::optional<Event> poll(std::chrono::milliseconds timeout = std::chrono::milliseconds{0});

    /// @return Whether a turn has been asked for and not collected yet.
    [[nodiscard]] bool isBusy() const noexcept;

    /// @return The conversation so far, in chronological order.
    [[nodiscard]] const std::vector<ChatMessage>& history() const noexcept {
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

    /// Builds the request of one turn.
    /// @param prompt Text the turn answers.
    /// @param instruction Contract that goes with the prompt, empty when there is none.
    /// @return The messages to send, the system prompt first.
    [[nodiscard]] std::vector<ChatMessage> buildRequest(const std::string& prompt,
                                                        std::string_view instruction) const;

    std::vector<ChatMessage> m_history;  ///< Conversation context owned by this brain.
    std::shared_ptr<OpenAnima::Provider::IProvider> m_provider;  ///< Backend for every request.
    std::string m_systemPrompt;            ///< Instruction leading every request.
    std::unique_ptr<TurnWorker> m_worker;  ///< Runs the async turns, created on first use.

    /// The turn that has been asked for and not collected yet.
    ///
    /// It is written into the conversation only when it is over and whole, and what it is
    /// worth writing is decided in one place: the prompt the player typed, as it was, not with
    /// the instruction that shaped it. The rounds of a future turn lean on this: an
    /// intermediary round is not a turn, it does not enter the history, and only the last
    /// reply is kept.
    struct PendingTurn {
        std::string prompt;  ///< What the player said, as it will be read back later.
        bool reply{false};   ///< Whether the turn asks for a reply document.
    };

    std::optional<PendingTurn> m_pending;  ///< Turn in flight, waiting for its answer.
    ActionCatalogue m_catalogue{ActionCatalogue::base()};  ///< Actions this NPC may take.
};

}  // namespace OpenAnima::Core
