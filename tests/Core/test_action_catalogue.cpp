#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>

#include "OpenAnima/Core/ActionCatalogue.h"

using OpenAnima::Core::ActionCatalogue;
using OpenAnima::Core::ActionSpec;

// Catch2 assertions expand into do-while blocks and test cases are long by design, so the
// analyzer reads both as findings. Neither fits production code.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("ActionCatalogue::base offers the actions every NPC starts with", "[catalogue]") {
    const ActionCatalogue catalogue = ActionCatalogue::base();

    REQUIRE(catalogue.actions().size() == 3);
    CHECK(catalogue.actions()[0].name == "wait");
    CHECK(catalogue.actions()[1].name == "end_conversation");
    CHECK(catalogue.actions()[2].name == "emote");
}

TEST_CASE("ActionCatalogue builds a reply schema with every key required", "[catalogue]") {
    const nlohmann::json schema = ActionCatalogue::base().replySchema();

    CHECK(schema["type"] == "object");
    CHECK(schema["additionalProperties"] == false);
    REQUIRE(schema["required"].is_array());
    CHECK(schema["required"].size() == 3);

    const nlohmann::json& properties = schema["properties"];
    CHECK(properties["reasoning"]["type"] == "string");
    CHECK(properties["dialogue"]["type"] == "string");
    CHECK(properties["actions"]["type"] == "array");
    CHECK_FALSE(properties["reasoning"]["description"].get<std::string>().empty());
    CHECK_FALSE(properties["dialogue"]["description"].get<std::string>().empty());

    const nlohmann::json alternatives = properties["actions"]["items"]["oneOf"];
    REQUIRE(alternatives.size() == 3);
    CHECK(alternatives[0]["properties"]["name"]["const"] == "wait");
    CHECK(alternatives[1]["properties"]["name"]["const"] == "end_conversation");
    CHECK(alternatives[2]["properties"]["name"]["const"] == "emote");
    CHECK(alternatives[2]["properties"]["arguments"]["properties"]["kind"]["type"] == "string");
    CHECK(alternatives[0]["properties"]["arguments"]["type"] == "object");
    CHECK(alternatives[0]["required"][0] == "name");
    CHECK(alternatives[0]["additionalProperties"] == false);
}

TEST_CASE("ActionCatalogue replaces an action that has its name", "[catalogue]") {
    ActionCatalogue catalogue = ActionCatalogue::base();

    catalogue.add(ActionSpec{"emote", "Wave a hand.", nlohmann::json{}});

    REQUIRE(catalogue.actions().size() == 3);
    CHECK(catalogue.actions()[2].description == "Wave a hand.");
}

TEST_CASE("ActionCatalogue adds a new action at the end", "[catalogue]") {
    ActionCatalogue catalogue = ActionCatalogue::base();

    catalogue.add(ActionSpec{"open", "Open a door.", nlohmann::json{}});

    REQUIRE(catalogue.actions().size() == 4);
    CHECK(catalogue.actions()[3].name == "open");
}

TEST_CASE("ActionCatalogue allows no action when it holds none", "[catalogue]") {
    const ActionCatalogue empty;

    const nlohmann::json actions = empty.replySchema()["properties"]["actions"];
    CHECK(actions["maxItems"] == 0);
    CHECK_FALSE(actions.contains("items"));
    CHECK(empty.replyInstruction().find("You cannot take actions.") != std::string::npos);
}

TEST_CASE("ActionCatalogue tells the model what to answer with", "[catalogue]") {
    const std::string instruction = ActionCatalogue::base().replyInstruction();

    CHECK(instruction.find("json") != std::string::npos);
    CHECK(instruction.find("reasoning") != std::string::npos);
    CHECK(instruction.find("dialogue") != std::string::npos);
    CHECK(instruction.find("actions") != std::string::npos);
    CHECK(instruction.find("- wait:") != std::string::npos);
    CHECK(instruction.find("- end_conversation:") != std::string::npos);
    CHECK(instruction.find("- emote:") != std::string::npos);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
