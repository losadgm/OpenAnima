#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <utility>

#include "OpenAnima/OpenAnima.h"

#include "support/fake_http_transport.h"

using OpenAnima::Core::NPCBrain;
using OpenAnima::Provider::RestProviderConfig;
using OpenAnima::TestSupport::FakeHttpTransport;

// Catch2 assertions expand into do-while blocks and test cases are long by design, so
// the rules written for production code do not fit here.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("OpenAnima::makeNpc wires the config and transport into an NPC", "[facade]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, R"({"choices":[{"message":{"content":"hi"}}]})"};

    RestProviderConfig config;
    config.baseUrl = "http://localhost:11434/v1";
    config.model = "test-model";

    NPCBrain npc = OpenAnima::makeNpc(std::move(config), transport);

    const auto answer = npc.ask("hello");

    REQUIRE(answer.text == "hi");
    REQUIRE(transport->requests.size() == 1);
    CHECK(transport->requests[0].url == "http://localhost:11434/v1/chat/completions");
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
