#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "OpenAnima/Core/Event.h"
#include "OpenAnima/Core/NPCBrain.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Errors.h"

#include "support/fake_provider.h"

using OpenAnima::OpenAnimaError;
using OpenAnima::Core::Event;
using OpenAnima::Core::EventKind;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::NPCBrain;
using OpenAnima::Core::RequestId;
using OpenAnima::Core::Role;
using OpenAnima::TestSupport::FakeProvider;

// Catch2 assertions expand into do-while blocks and test cases are long by design, so
// the rules written for production code do not fit here.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("NPCBrain::ping reports a successful initialization", "[npcbrain]") {
    NPCBrain brain;
    CHECK(brain.ping() == "OpenAnima core is linked and operational.");
}

TEST_CASE("NPCBrain::ask returns the answer and keeps the turn", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    provider->answer.text = "hello, traveller";
    NPCBrain brain{provider};

    CHECK(brain.ask("hi").text == "hello, traveller");

    REQUIRE(brain.history().size() == 2);
    CHECK(brain.history()[0].role == Role::User);
    CHECK(brain.history()[0].content == "hi");
    CHECK(brain.history()[1].role == Role::Assistant);
    CHECK(brain.history()[1].content == "hello, traveller");
}

TEST_CASE("NPCBrain::ask sends the earlier turns back to the backend", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    NPCBrain brain{provider};

    (void)brain.ask("first");
    (void)brain.ask("second");

    REQUIRE(provider->requests.size() == 2);
    REQUIRE(provider->requests[1].size() == 3);
    CHECK(provider->requests[1][0].role == Role::User);
    CHECK(provider->requests[1][0].content == "first");
    CHECK(provider->requests[1][1].role == Role::Assistant);
    CHECK(provider->requests[1][2].content == "second");
}

TEST_CASE("NPCBrain::ask leads the request with the system prompt", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    NPCBrain brain{provider};
    brain.setSystemPrompt("You are a grumpy blacksmith.");

    (void)brain.ask("hi");

    REQUIRE(provider->requests[0].size() == 2);
    CHECK(provider->requests[0][0].role == Role::System);
    CHECK(provider->requests[0][0].content == "You are a grumpy blacksmith.");
    // The persona is resolved per request, so it never enters the stored conversation.
    CHECK(brain.history().size() == 2);
}

TEST_CASE("NPCBrain::ask leaves the history untouched when the call fails", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    provider->failure = "connection refused";
    NPCBrain brain{provider};

    CHECK_THROWS_AS(brain.ask("hi"), OpenAnimaError);
    CHECK(brain.history().empty());
}

TEST_CASE("NPCBrain::ask reports a missing backend", "[npcbrain]") {
    NPCBrain brain;

    CHECK_THROWS_AS(brain.ask("hi"), OpenAnimaError);
}

TEST_CASE("NPCBrain::clearHistory drops the conversation and nothing else", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    NPCBrain brain{provider};
    brain.setSystemPrompt("stay in character");
    (void)brain.ask("hi");

    brain.clearHistory();
    CHECK(brain.history().empty());

    (void)brain.ask("again");
    REQUIRE(provider->requests.back().size() == 2);
    CHECK(provider->requests.back()[0].role == Role::System);
    CHECK(provider->requests.back()[1].content == "again");
}

/// Holds the backend inside its answer, so that a turn can be abandoned at a point
/// the test chooses instead of at a point it guesses.
class Halt {
public:
    /// @param at Piece before which the backend waits.
    explicit Halt(int at) : m_at(at) {}

    /// Runs before each piece the backend produces.
    void operator()() {
        std::unique_lock<std::mutex> lock{m_mutex};
        if (++m_calls != m_at) {
            return;
        }
        m_cv.wait(lock, [this] { return m_open; });
    }

    /// Lets the backend carry on.
    void release() {
        {
            std::lock_guard<std::mutex> lock{m_mutex};
            m_open = true;
        }
        m_cv.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    int m_at;            ///< Call that waits.
    int m_calls{0};      ///< Calls seen so far.
    bool m_open{false};  ///< Whether the waiting call may return.
};

/// Collects events until the turn ends, so each test asserts only what it is about.
/// @param brain Brain to drain.
/// @return Every event of the turn left to collect, in the order it arrived.
std::vector<Event> drain(NPCBrain& brain) {
    std::vector<Event> events;
    for (;;) {
        std::optional<Event> event = brain.poll(std::chrono::milliseconds{5000});
        REQUIRE(event.has_value());
        const bool ends = event->kind == EventKind::Finished || event->kind == EventKind::Failed;
        events.push_back(*event);
        if (ends) {
            return events;
        }
    }
}

TEST_CASE("NPCBrain::askAsync reports a turn piece by piece", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    provider->deltas = {"under", "stood"};
    NPCBrain brain{provider};

    const RequestId id = brain.askAsync("hi");
    CHECK(brain.isBusy());

    const std::vector<Event> events = drain(brain);
    REQUIRE(events.size() == 4);
    CHECK(events[0].kind == EventKind::Started);
    CHECK(events[1].kind == EventKind::Delta);
    CHECK(events[1].text == "under");
    CHECK(events[2].kind == EventKind::Delta);
    CHECK(events[2].text == "stood");
    CHECK(events[3].kind == EventKind::Finished);
    CHECK(events[3].completion.text == "understood");
    for (const Event& event : events) {
        CHECK(event.id == id);
    }

    // The turn joins the conversation when its answer has been collected, not when
    // the backend stopped talking.
    CHECK_FALSE(brain.isBusy());
    REQUIRE(brain.history().size() == 2);
    CHECK(brain.history()[0].content == "hi");
    CHECK(brain.history()[1].content == "understood");
}

TEST_CASE("NPCBrain::askAsync stops a turn the caller abandons", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    provider->deltas = {"half", " a", " thought"};
    Halt halt{2};  // The backend waits before its second piece.
    provider->beforeEachDelta = [&halt] { halt(); };
    NPCBrain brain{provider};

    const RequestId id = brain.askAsync("think");

    std::optional<Event> seen = brain.poll(std::chrono::milliseconds{5000});
    while (seen.has_value() && seen->kind != EventKind::Delta) {
        seen = brain.poll(std::chrono::milliseconds{5000});
    }
    REQUIRE(seen.has_value());
    CHECK(seen->text == "half");

    brain.cancel(id);
    halt.release();

    const std::vector<Event> events = drain(brain);
    REQUIRE(events.size() == 1);  // The pieces stopped coming; only the end was left.
    CHECK(events[0].kind == EventKind::Finished);
    CHECK(events[0].completion.finishReason == FinishReason::Cancelled);
    CHECK(events[0].completion.text == "half");

    // What the player got to read is part of the conversation, cut short as it is.
    REQUIRE(brain.history().size() == 2);
    CHECK(brain.history()[0].content == "think");
    CHECK(brain.history()[1].content == "half");
}

TEST_CASE("NPCBrain::cancel ignores a turn that is not the one in flight", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    provider->deltas = {"understood"};
    NPCBrain brain{provider};

    const RequestId id = brain.askAsync("hi");
    brain.cancel(id + 1);  // Some other turn, from some other time.

    const std::vector<Event> events = drain(brain);
    CHECK(events.back().completion.finishReason != FinishReason::Cancelled);
    CHECK(events.back().completion.text == "understood");

    brain.cancel(id);  // Already collected: ignored, not an error.
    CHECK_FALSE(brain.isBusy());
}

TEST_CASE("NPCBrain::askAsync refuses a turn while another one is in flight", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    provider->deltas = {"understood"};
    Halt halt{1};  // The backend waits before it produces anything.
    provider->beforeEachDelta = [&halt] { halt(); };
    NPCBrain brain{provider};

    (void)brain.askAsync("first");
    CHECK(brain.isBusy());
    CHECK_THROWS_AS(brain.askAsync("second"), OpenAnimaError);

    halt.release();
    CHECK(drain(brain).back().kind == EventKind::Finished);
    CHECK_FALSE(brain.isBusy());

    // The brain is free again, and the conversation went on from where it was.
    CHECK_NOTHROW((void)brain.askAsync("second"));
}

TEST_CASE("NPCBrain::askAsync reports a backend that fails", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    provider->failure = "connection refused";
    NPCBrain brain{provider};

    (void)brain.askAsync("hi");

    const std::vector<Event> events = drain(brain);
    REQUIRE(events.size() == 2);
    CHECK(events[0].kind == EventKind::Started);
    CHECK(events[1].kind == EventKind::Failed);
    CHECK(events[1].error == "connection refused");

    // A turn that failed leaves the conversation as it was, the way ask() does.
    CHECK(brain.history().empty());
    CHECK_FALSE(brain.isBusy());
}

TEST_CASE("NPCBrain::askAsync reports a missing backend", "[npcbrain]") {
    NPCBrain brain;

    CHECK_THROWS_AS(brain.askAsync("hi"), OpenAnimaError);
    CHECK_FALSE(brain.isBusy());
}

TEST_CASE("NPCBrain::poll reports nothing when there is nothing to report", "[npcbrain]") {
    auto provider = std::make_shared<FakeProvider>();
    NPCBrain brain{provider};

    CHECK_FALSE(brain.poll().has_value());

    (void)brain.askAsync("hi");
    CHECK(drain(brain).back().kind == EventKind::Finished);
    CHECK_FALSE(brain.poll().has_value());
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
