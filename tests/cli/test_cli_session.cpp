#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "cliHarness/session.h"

using OpenAnimaCli::CommandHelp;
using OpenAnimaCli::commandHelp;
using OpenAnimaCli::OrderKind;
using OpenAnimaCli::Session;
using OpenAnimaCli::Step;

namespace {

/// @return Whether any of the notes says the thing.
bool says(const Step& step, const std::string& text) {
    return std::any_of(step.notes.begin(), step.notes.end(), [&text](const std::string& note) {
        return note.find(text) != std::string::npos;
    });
}

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
TEST_CASE("a session starts in the lobby, with no npc and no raw", "[cli]") {
    const Session session;

    REQUIRE_FALSE(session.active().has_value());
    REQUIRE_FALSE(session.raw());
    REQUIRE(session.prompt() == "[lobby] > ");
}

TEST_CASE("an npc is created and talked to", "[cli]") {
    Session session;
    const std::size_t npcId = session.create("a blacksmith of the village");

    REQUIRE(npcId == 1);
    REQUIRE(session.active() == npcId);
    // The prompt carries the id, never the persona: that one is as long as it wants to be.
    REQUIRE(session.prompt() == "[npc 1] > ");
}

TEST_CASE("an empty line asks for nothing", "[cli]") {
    Session session;
    session.create("herrero");
    const Step step = session.interpret("   ");

    REQUIRE(step.order.kind == OrderKind::Nothing);
    REQUIRE(step.notes.empty());
    REQUIRE_FALSE(step.quit);
}

TEST_CASE("a line that is not a command goes to the active npc", "[cli]") {
    Session session;
    session.create("herrero");
    const Step step = session.interpret("do you have any iron daggers?");

    REQUIRE(step.order.kind == OrderKind::Ask);
    REQUIRE(step.order.npcId == 1);
    REQUIRE(step.order.text == "do you have any iron daggers?");
    REQUIRE(step.notes.empty());
}

TEST_CASE("a line with no npc says how to make one", "[cli]") {
    Session session;
    const Step step = session.interpret("hola");

    REQUIRE(step.order.kind == OrderKind::Nothing);
    REQUIRE(says(step, "/create"));
    REQUIRE(says(step, "/enter"));
}

TEST_CASE("the /create command with no description says how it is written", "[cli]") {
    Session session;
    const Step step = session.interpret("/create");

    REQUIRE(step.order.kind == OrderKind::Nothing);
    REQUIRE(says(step, "usage: /create <description>"));
    REQUIRE_FALSE(session.active().has_value());
}

TEST_CASE("the /create command makes the npc and asks the shell for its brain", "[cli]") {
    Session session;
    const Step step = session.interpret("/create  a blacksmith");

    REQUIRE(step.order.kind == OrderKind::CreateNpc);
    REQUIRE(step.order.npcId == 1);
    REQUIRE(step.order.text == "a blacksmith");
    REQUIRE(says(step, "npc 1 created"));
    REQUIRE(session.active() == 1);
}

TEST_CASE("the /npcs command lists what exists and marks the active one", "[cli]") {
    Session session;
    REQUIRE(says(session.interpret("/npcs"), "no npcs yet"));

    session.interpret("/create herrero");
    session.interpret("/create hermana");
    session.interpret("/enter 1");
    const Step step = session.interpret("/npcs");

    REQUIRE(step.notes.size() == 2);
    REQUIRE(step.notes[0] == "* 1  herrero");
    REQUIRE(step.notes[1] == "  2  hermana");
    REQUIRE(says(session.interpret("/npcs now"), "usage: /npcs"));
}

TEST_CASE("the /enter command checks its argument before touching the active npc", "[cli]") {
    Session session;
    session.interpret("/create herrero");

    REQUIRE(says(session.interpret("/enter"), "usage: /enter <id>"));
    REQUIRE(says(session.interpret("/enter two"), "a number from /npcs"));
    REQUIRE(says(session.interpret("/enter 7"), "no such npc"));
    REQUIRE(session.active() == 1);

    REQUIRE(says(session.interpret("/enter 1"), "talking to npc 1"));
    REQUIRE(session.active() == 1);
}

TEST_CASE("the /leave command goes back to the lobby once", "[cli]") {
    Session session;
    session.interpret("/create herrero");

    const Step step = session.interpret("/leave");
    REQUIRE(says(step, "in the lobby"));
    REQUIRE_FALSE(session.active().has_value());
    REQUIRE(session.prompt() == "[lobby] > ");

    REQUIRE(says(session.interpret("/leave"), "already in the lobby"));
    REQUIRE(says(session.interpret("/leave now"), "usage: /leave"));
}

TEST_CASE("the /clear command drops the conversation of the active npc only", "[cli]") {
    Session session;
    REQUIRE(says(session.interpret("/clear"), "no npc"));

    session.interpret("/create herrero");
    const Step step = session.interpret("/clear");

    REQUIRE(step.order.kind == OrderKind::Clear);
    REQUIRE(step.order.npcId == 1);
    REQUIRE(says(step, "conversation cleared"));
    REQUIRE(says(session.interpret("/clear later"), "usage: /clear"));
}

TEST_CASE("the /raw command toggles, and takes on and off", "[cli]") {
    Session session;
    REQUIRE(says(session.interpret("/raw"), "raw json on"));
    REQUIRE(session.raw());
    REQUIRE(says(session.interpret("/raw"), "raw json off"));
    REQUIRE_FALSE(session.raw());

    REQUIRE(says(session.interpret("/raw on"), "raw json on"));
    REQUIRE(session.raw());
    REQUIRE(says(session.interpret("/raw off"), "raw json off"));
    REQUIRE_FALSE(session.raw());

    REQUIRE(says(session.interpret("/raw perhaps"), "usage: /raw [on|off]"));
    REQUIRE_FALSE(session.raw());
}

TEST_CASE("the /help command lists every command there is", "[cli]") {
    Session session;
    const Step step = session.interpret("/help");

    REQUIRE(step.notes.size() == commandHelp().size());
    for (const CommandHelp& command : commandHelp()) {
        REQUIRE(says(step, command.usage));
    }
    REQUIRE(says(session.interpret("/help me"), "usage: /help"));
}

TEST_CASE("the /quit command leaves, and takes no argument", "[cli]") {
    Session session;
    const Step step = session.interpret("/quit");

    REQUIRE(step.quit);
    REQUIRE(step.order.kind == OrderKind::Nothing);
    REQUIRE(says(session.interpret("/quit now"), "usage: /quit"));
}

TEST_CASE("an unknown command says where to look", "[cli]") {
    Session session;
    const Step step = session.interpret("/shout");

    REQUIRE(step.order.kind == OrderKind::Nothing);
    REQUIRE(says(step, "unknown command '/shout'"));
    REQUIRE(says(step, "/help"));
}

TEST_CASE("surrounding blanks do not change a command", "[cli]") {
    Session session;
    session.interpret("/create herrero");
    const Step step = session.interpret("   /npcs   ");

    REQUIRE(step.notes.size() == 1);
    REQUIRE(step.notes[0] == "* 1  herrero");
}
// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
