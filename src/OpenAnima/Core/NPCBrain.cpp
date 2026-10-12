#include "OpenAnima/Core/NPCBrain.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/IProvider.h"

namespace OpenAnima::Core {

namespace {

constexpr const char* PingMessage = "OpenAnima core is linked and operational.";

/// One turn handed to the background thread.
///
/// The backend and the conversation are copied in on the caller's thread, which
/// stays the only thread that reads the brain.
struct Job {
    RequestId id{};                                            ///< Turn this job runs.
    std::shared_ptr<OpenAnima::Provider::IProvider> provider;  ///< Backend that answers it.
    std::vector<ChatMessage> messages;                         ///< Conversation to send.
};

/// Builds an event with only what every kind of event carries.
/// @param turnId Turn the event belongs to.
/// @param kind What happened.
/// @return The event, with the fields of its kind still empty.
Event makeEvent(RequestId turnId, EventKind kind) {
    Event event;
    event.id = turnId;
    event.kind = kind;
    return event;
}

}  // namespace

/// Runs one turn at a time on its own thread and queues what it reports.
class NPCBrain::TurnWorker {
public:
    TurnWorker() = default;
    TurnWorker(const TurnWorker&) = delete;
    TurnWorker& operator=(const TurnWorker&) = delete;
    TurnWorker(TurnWorker&&) = delete;  // Binds its threads to itself, so it stays put.
    TurnWorker& operator=(TurnWorker&&) = delete;

    /// Stops the turn in flight and waits for the thread to end.
    ///
    /// Stopping means asking the backend to stop, so this lasts as long as the
    /// backend takes to notice. With the shipped HTTP transport that is the idle
    /// check libcurl runs on its own timer, well under a second.
    ~TurnWorker() {
        stop();
    }

    /// Hands a turn to the thread.
    /// @param messages Conversation to send, system prompt included.
    /// @param provider Backend that answers it.
    /// @return The id of the turn.
    /// @throws OpenAnimaError when a turn is already in flight.
    RequestId start(std::vector<ChatMessage> messages,
                    std::shared_ptr<OpenAnima::Provider::IProvider> provider) {
        if (m_busy) {
            throw OpenAnimaError("a turn is already in flight: collect its events, or cancel it");
        }

        const RequestId turnId = ++m_lastId;
        m_current.store(turnId);
        m_abandoned.store(0);
        m_busy = true;

        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_job = Job{turnId, std::move(provider), std::move(messages)};
        }
        if (!m_thread.joinable()) {
            m_thread = std::thread{&NPCBrain::TurnWorker::run, this};
        }
        m_jobReady.notify_one();
        return turnId;
    }

    /// Asks the turn in flight to stop.
    /// @param turnId Turn to stop as handed out by start(); other ids are ignored.
    void cancel(RequestId turnId) {
        if (turnId != m_current.load()) {
            return;
        }
        m_abandoned.store(turnId);
    }

    /// @param timeout Longest wait for an event.
    /// @return The next event, or nothing when none was queued in time.
    std::optional<Event> takeEvent(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock{m_mutex};
        m_eventReady.wait_for(lock, timeout, [this] { return !m_events.empty(); });
        if (m_events.empty()) {
            return std::nullopt;
        }
        Event event = std::move(m_events.front());
        m_events.pop_front();
        return event;
    }

    /// Marks the turn in flight as collected, freeing the brain for the next one.
    void collect() {
        m_busy = false;
        m_current.store(0);
    }

    /// @return Whether a turn was handed over and not collected yet.
    [[nodiscard]] bool busy() const noexcept {
        return m_busy;
    }

private:
    /// Takes turns from the queue and runs them until the worker is stopped.
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock{m_mutex};
                m_jobReady.wait(lock, [this] { return m_job.has_value() || m_stopping.load(); });
                if (!m_job.has_value()) {
                    return;  // Stopped with nothing left to run.
                }
                job = std::move(*m_job);
                m_job.reset();
            }
            if (m_stopping.load()) {
                return;
            }
            runTurn(job);
        }
    }

    /// Runs one turn and publishes how it ended. Nothing escapes: a backend that
    /// throws becomes a Failed event instead of a dead thread.
    /// @param job Turn to run.
    void runTurn(const Job& job) {
        push(makeEvent(job.id, EventKind::Started));

        OpenAnima::Provider::Call call;
        call.onDelta = [this, turnId = job.id](std::string_view piece) {
            Event event = makeEvent(turnId, EventKind::Delta);
            event.text = std::string{piece};
            push(std::move(event));
        };
        call.keepGoing = [this, turnId = job.id] {
            return !m_stopping.load() && m_abandoned.load() != turnId;
        };

        try {
            Event event = makeEvent(job.id, EventKind::Finished);
            event.completion = job.provider->complete(job.messages, call);
            push(std::move(event));
        } catch (const std::exception& failure) {
            Event event = makeEvent(job.id, EventKind::Failed);
            event.error = failure.what();
            push(std::move(event));
        }
    }

    /// Adds an event to the queue and wakes whoever waits for one.
    /// @param event Event to publish.
    void push(Event event) {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_events.push_back(std::move(event));
        }
        m_eventReady.notify_one();
    }

    /// Stops the thread: the turn in flight is abandoned and queued work dropped.
    void stop() {
        if (!m_thread.joinable()) {
            return;
        }
        m_stopping.store(true);
        m_abandoned.store(m_current.load());
        m_jobReady.notify_all();
        m_thread.join();
    }

    std::mutex m_mutex;                     ///< Guards the job and the event queue.
    std::condition_variable m_jobReady;     ///< Wakes the thread when a turn arrives.
    std::condition_variable m_eventReady;   ///< Wakes the caller when an event arrives.
    std::deque<Event> m_events;             ///< Produced events not collected yet.
    std::optional<Job> m_job;               ///< Turn waiting for the thread.
    std::thread m_thread;                   ///< Thread that runs the turns.
    std::atomic<RequestId> m_current{0};    ///< Turn in flight, zero when there is none.
    std::atomic<RequestId> m_abandoned{0};  ///< Turn the caller asked to stop.
    std::atomic<bool> m_stopping{false};    ///< Set while the worker is being torn down.
    RequestId m_lastId{0};                  ///< Last id handed out, touched by callers only.
    bool m_busy{false};                     ///< Handed over and not collected, callers only.
};

NPCBrain::NPCBrain() = default;

NPCBrain::NPCBrain(std::shared_ptr<OpenAnima::Provider::IProvider> provider)
    : m_provider(std::move(provider)) {
    assert(m_provider != nullptr && "NPCBrain requires a provider");
}

// Out of line: the worker behind the pointer is only known here.
NPCBrain::~NPCBrain() = default;

NPCBrain::NPCBrain(NPCBrain&&) noexcept = default;

NPCBrain& NPCBrain::operator=(NPCBrain&&) noexcept = default;

void NPCBrain::setProvider(std::shared_ptr<OpenAnima::Provider::IProvider> provider) {
    assert(provider != nullptr && "setProvider requires a provider");
    m_provider = std::move(provider);
}

void NPCBrain::setSystemPrompt(std::string prompt) {
    m_systemPrompt = std::move(prompt);
}

Completion NPCBrain::ask(const std::string& prompt) {
    return ask(prompt, Provider::Call{});
}

Completion NPCBrain::ask(const std::string& prompt, const Provider::Call& call) {
    assert(!prompt.empty() && "ask() requires a non-empty prompt");

    if (m_provider == nullptr) {
        throw OpenAnimaError("no backend is attached to this brain: call setProvider() first");
    }

    std::vector<ChatMessage> request;
    request.reserve(m_history.size() + 2);
    if (!m_systemPrompt.empty()) {
        request.push_back(ChatMessage{Role::System, m_systemPrompt});
    }
    request.insert(request.end(), m_history.begin(), m_history.end());
    request.push_back(ChatMessage{Role::User, prompt});

    // The answer is fetched before the history is modified, so a failed call leaves the
    // conversation exactly as it was and the client can retry the same turn.
    const Completion answer = m_provider->complete(request, call);

    m_history.push_back(ChatMessage{Role::User, prompt});
    m_history.push_back(ChatMessage{Role::Assistant, answer.text});
    return answer;
}

RequestId NPCBrain::askAsync(std::string prompt) {
    assert(!prompt.empty() && "askAsync() requires a non-empty prompt");

    if (m_provider == nullptr) {
        throw OpenAnimaError("no backend is attached to this brain: call setProvider() first");
    }

    std::vector<ChatMessage> request;
    request.reserve(m_history.size() + 2);
    if (!m_systemPrompt.empty()) {
        request.push_back(ChatMessage{Role::System, m_systemPrompt});
    }
    request.insert(request.end(), m_history.begin(), m_history.end());
    request.push_back(ChatMessage{Role::User, prompt});

    if (m_worker == nullptr) {
        m_worker = std::make_unique<TurnWorker>();
    }
    const RequestId turnId = m_worker->start(std::move(request), m_provider);

    // Kept here, on the caller's thread, because the history belongs to it: the
    // turn is written down when its last event is collected, not when it ends.
    m_pendingPrompt = std::move(prompt);
    return turnId;
}

void NPCBrain::cancel(RequestId turnId) {
    if (m_worker != nullptr) {
        m_worker->cancel(turnId);
    }
}

std::optional<Event> NPCBrain::poll(std::chrono::milliseconds timeout) {
    if (m_worker == nullptr) {
        return std::nullopt;
    }

    std::optional<Event> event = m_worker->takeEvent(timeout);
    if (!event.has_value()) {
        return std::nullopt;
    }

    const bool endsTheTurn = event->kind == EventKind::Finished || event->kind == EventKind::Failed;
    if (!endsTheTurn) {
        return event;
    }

    // A turn that was made joins the conversation; one that failed leaves it as it
    // was, the same way a failed ask() does.
    if (event->kind == EventKind::Finished) {
        m_history.push_back(ChatMessage{Role::User, m_pendingPrompt});
        m_history.push_back(ChatMessage{Role::Assistant, event->completion.text});
    }
    m_pendingPrompt.clear();
    m_worker->collect();
    return event;
}

bool NPCBrain::isBusy() const noexcept {
    return m_worker != nullptr && m_worker->busy();
}

void NPCBrain::clearHistory() {
    m_history.clear();
}

// Part of the public instance API; kept non-static on purpose.
std::string NPCBrain::ping() {  // NOLINT(readability-convert-member-functions-to-static)
    return PingMessage;
}

}  // namespace OpenAnima::Core
