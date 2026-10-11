#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

#include "OpenAnima/Core/NPCBrain.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Errors.h"

#include "support/fake_provider.h"

using OpenAnima::OpenAnimaError;
using OpenAnima::Core::NPCBrain;
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

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
