#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "OpenAnima/Core/NpcReply.h"
#include "OpenAnima/Core/ReplyStream.h"
#include "OpenAnima/Errors.h"

using OpenAnima::ParseError;
using OpenAnima::Core::Completion;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::NpcReply;
using OpenAnima::Core::ReplyStream;

namespace {

/// Builds the answer a provider hands back when a reply was asked for.
/// @param document JSON the model wrote.
/// @return The completion, with the document already read.
Completion answered(std::string document) {
    Completion completion;
    completion.text = std::move(document);
    completion.json = nlohmann::json::parse(completion.text);
    completion.finishReason = FinishReason::Stop;
    return completion;
}

}  // namespace

// Catch2 assertions expand into do-while blocks and test cases are long by design, so the
// analyzer reads both as findings. Neither fits production code.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("readReply splits the three variables", "[reply]") {
    const Completion completion =
        answered(R"({"reasoning":"He looks harmless.","dialogue":"Good evening.","actions":[)"
                 R"({"name":"emote","arguments":{"kind":"nod"}}]})");

    const NpcReply reply = readReply(completion);

    CHECK(reply.reasoning == "He looks harmless.");
    CHECK(reply.dialogue == "Good evening.");
    REQUIRE(reply.actions.size() == 1);
    CHECK(reply.actions[0].name == "emote");
    CHECK(reply.actions[0].arguments["kind"] == "nod");
    CHECK(reply.document.is_object());
    CHECK(reply.completion.text == completion.text);
}

TEST_CASE("readReply accepts an answer with nothing to say or to do", "[reply]") {
    const NpcReply reply = readReply(answered(R"({"reasoning":"","dialogue":"","actions":[]})"));

    CHECK(reply.reasoning.empty());
    CHECK(reply.dialogue.empty());
    CHECK(reply.actions.empty());
    CHECK(reply.document.is_object());
}

TEST_CASE("readReply refuses a reply without its keys", "[reply]") {
    CHECK_THROWS_AS(readReply(answered(R"({"dialogue":"hi","actions":[]})")), ParseError);
    CHECK_THROWS_AS(readReply(answered(R"({"reasoning":"x","actions":[]})")), ParseError);
    CHECK_THROWS_AS(readReply(answered(R"({"reasoning":"x","dialogue":"hi"})")), ParseError);
    CHECK_THROWS_AS(readReply(answered(R"({"reasoning":"x","dialogue":"hi","actions":{}})")),
                    ParseError);
}

TEST_CASE("readReply refuses an action that is not a name and its arguments", "[reply]") {
    CHECK_THROWS_AS(
        readReply(answered(R"({"reasoning":"x","dialogue":"hi","actions":[{"name":"wait"}]})")),
        ParseError);
    CHECK_THROWS_AS(
        readReply(answered(R"({"reasoning":"x","dialogue":"hi","actions":[{"arguments":{}}]})")),
        ParseError);
    CHECK_THROWS_AS(readReply(answered(R"({"reasoning":"x","dialogue":"hi","actions":["wait"]})")),
                    ParseError);
}

TEST_CASE("readReply refuses an answer that was not read as JSON", "[reply]") {
    Completion completion;
    completion.text = "Good evening.";
    completion.finishReason = FinishReason::Stop;

    CHECK_THROWS_AS(readReply(completion), ParseError);
}

TEST_CASE("readReply keeps what a cancelled turn had said", "[reply]") {
    Completion completion;
    completion.text = R"({"reasoning":"He looks","dialogue":"Good eve)";
    completion.finishReason = FinishReason::Cancelled;

    const NpcReply reply = readReply(completion, "Good eve");

    CHECK(reply.dialogue == "Good eve");
    CHECK(reply.reasoning.empty());
    CHECK(reply.actions.empty());
    CHECK(reply.document.is_null());
    CHECK(reply.completion.finishReason == FinishReason::Cancelled);
}

TEST_CASE("ReplyStream reads the dialogue out of a whole document", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    const std::string document =
        R"({"reasoning":"Careful.","dialogue":"Hello, traveller.","actions":[]})";

    CHECK(stream.feed(document) == "Hello, traveller.");
    CHECK(stream.text() == "Hello, traveller.");
}

TEST_CASE("ReplyStream hands out the dialogue while it is being written", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"reasoning":"Careful.",)") == "");
    CHECK(stream.feed(R"("dialogue":"Good )") == "Good ");
    CHECK(stream.feed(R"(evening.",)") == "evening.");
    CHECK(stream.feed(R"("actions":[]})") == "");
    CHECK(stream.text() == "Good evening.");
}

TEST_CASE("ReplyStream does not care about the order of the keys", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"dialogue":"Hi","reasoning":"x","actions":[]})") == "Hi");
}

TEST_CASE("ReplyStream ignores the same key inside another object", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"details":{"dialogue":"no"},"dialogue":"yes"})") == "yes");
}

TEST_CASE("ReplyStream ignores the key when it is only part of another text", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"reasoning":"the word dialogue","dialogue":"yes"})") == "yes");
}

TEST_CASE("ReplyStream decodes the escapes of the dialogue", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"dialogue":"line\none \"quoted\"!"})") == "line\none \"quoted\"!");
}

TEST_CASE("ReplyStream reads an escape split across two pieces", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"dialogue":"a\)") == "a");
    CHECK(stream.feed("n") == "\n");
    CHECK(stream.feed(R"(b"})") == "b");
    CHECK(stream.text() == "a\nb");
}

TEST_CASE("ReplyStream reads a unicode escape split across two pieces", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"dialogue":"caf\u00)") == "caf");
    CHECK(stream.feed(R"(e9"})") == "\xc3\xa9");
    CHECK(stream.text() == "caf\xc3\xa9");
}

TEST_CASE("ReplyStream joins the two halves of a surrogate pair", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"dialogue":"\ud83d)") == "");
    CHECK(stream.feed(R"(\ude00"})") == "\xf0\x9f\x98\x80");
}

TEST_CASE("ReplyStream keeps what arrived before the document was cut", "[reply][stream]") {
    ReplyStream stream{"dialogue"};

    CHECK(stream.feed(R"({"reasoning":"Careful.","dialogue":"Good eve)") == "Good eve");
    CHECK(stream.text() == "Good eve");
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
