#include <catch2/catch_test_macros.hpp>
#include <memory>

#include "OpenAnima/OpenAnima.h"

#include "support/fake_http_transport.h"

using OpenAnima::Core::NPCBrain;
using OpenAnima::Provider::ProviderConfig;
using OpenAnima::TestSupport::FakeHttpTransport;

// Catch2 assertions expand into do-while blocks and test cases are long by design, so
// the rules written for production code do not fit here.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("OpenAnima::makeNpc wires the config and transport into an NPC", "[facade]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, R"({"choices":[{"message":{"content":"hi"}}]})"};

    NPCBrain npc =
        OpenAnima::makeNpc(ProviderConfig{"http://localhost:11434/v1", "test-model"}, transport);

    const auto answer = npc.ask("hello");

    REQUIRE(answer.text == "hi");
    REQUIRE(transport->requests.size() == 1);
    CHECK(transport->requests[0].url == "http://localhost:11434/v1/chat/completions");
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
