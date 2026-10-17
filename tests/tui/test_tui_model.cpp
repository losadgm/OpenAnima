/// @file
/// @brief What a key does to the world and to the prompt.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "OpenAnima/Core/Event.h"

#include "app/model.h"

namespace {

using OpenAnimaTui::Key;
using OpenAnimaTui::App::Effect;
using OpenAnimaTui::App::EffectKind;
using OpenAnimaTui::App::Message;
using OpenAnimaTui::App::Model;

constexpr std::size_t ScrollStep = 5;

Key key(const Key::Kind kind) {
    return Key{kind, 0};
}

Key character(const char value) {
    return Key{Key::Kind::Character, value};
}

std::vector<Effect> press(Model& model, const Key::Kind kind) {
    return OpenAnimaTui::App::update(model, key(kind), ScrollStep);
}

/// Feeds a draft the way a terminal does: one byte per key, accents included.
void type(Model& model, const std::string& text) {
    for (const char value : text) {
        (void)OpenAnimaTui::App::update(model, character(value), ScrollStep);
    }
}

std::vector<Effect> submit(Model& model, const std::string& text) {
    type(model, text);
    return press(model, Key::Kind::Enter);
}

/// A model with one NPC, made the way a user makes one.
Model modelWithNpc() {
    Model model;
    (void)submit(model, "/create a smith");
    return model;
}

bool hasEffect(const std::vector<Effect>& effects, const EffectKind kind) {
    return std::any_of(effects.begin(), effects.end(),
                       [kind](const Effect& effect) { return effect.kind == kind; });
}

OpenAnima::Core::Event delta(const std::string& text) {
    OpenAnima::Core::Event event;
    event.kind = OpenAnima::Core::EventKind::Delta;
    event.text = text;
    return event;
}

OpenAnima::Core::Event finished() {
    OpenAnima::Core::Event event;
    event.kind = OpenAnima::Core::EventKind::Finished;
    event.completion.finishReason = OpenAnima::Core::FinishReason::Stop;
    return event;
}

/// Answers the turn in flight the way the shell would when the stream ends.
void answer(Model& model, const std::string& text) {
    (void)OpenAnimaTui::App::applyEvent(model, delta(text));
    (void)OpenAnimaTui::App::applyEvent(model, finished());
}

/// A turn that came back as a reply, with the document the model wrote.
OpenAnima::Core::Event finishedWithReply(const std::string& document) {
    OpenAnima::Core::Event event = finished();
    OpenAnima::Core::NpcReply reply;
    reply.reasoning = "It looks harmless.";
    reply.dialogue = "Good evening.";
    reply.actions.push_back({"emote", nlohmann::ordered_json{{"kind", "nod"}}});
    reply.document = nlohmann::ordered_json::parse(document);
    event.reply = reply;
    return event;
}

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("a tab is never typed, because the terminal does not draw it as one column") {
    Model model;
    type(model, "abc");
    (void)press(model, Key::Kind::Tab);
    CHECK(model.draft == "abc");
}

TEST_CASE("tab completes the hint that is selected") {
    Model model;
    type(model, "/c");
    (void)press(model, Key::Kind::Tab);
    CHECK(model.draft == "/create ");
    CHECK(model.cursor == model.draft.size());
}

TEST_CASE("the arrows walk the hints while they are showing") {
    Model model;
    type(model, "/");
    CHECK(!OpenAnimaTui::App::hintsFor(model).empty());

    (void)press(model, Key::Kind::ArrowUp);
    (void)press(model, Key::Kind::Tab);
    CHECK(model.draft == "/quit ");

    Model other;
    type(other, "/");
    (void)press(other, Key::Kind::ArrowDown);
    (void)press(other, Key::Kind::Tab);
    CHECK(other.draft == "/enter ");
}

TEST_CASE("the arrows walk the prompts of the scope while no hint is showing") {
    Model model;
    (void)submit(model, "/npcs");
    (void)submit(model, "/help");
    CHECK(OpenAnimaTui::App::hintsFor(model).empty());

    (void)press(model, Key::Kind::ArrowUp);
    CHECK(model.draft == "/help");
    (void)press(model, Key::Kind::ArrowUp);
    CHECK(model.draft == "/npcs");
    (void)press(model, Key::Kind::ArrowDown);
    CHECK(model.draft == "/help");
    (void)press(model, Key::Kind::ArrowDown);
    CHECK(model.draft.empty());
}

TEST_CASE("each scope keeps its own history") {
    Model model = modelWithNpc();
    CHECK(OpenAnimaTui::App::update(model, key(Key::Kind::None), ScrollStep).empty());

    (void)submit(model, "/help");
    (void)press(model, Key::Kind::ArrowUp);
    CHECK(model.draft == "/help");
    (void)press(model, Key::Kind::ArrowUp);
    CHECK(model.draft == "/help");

    (void)press(model, Key::Kind::Enter);
    (void)submit(model, "/leave");
    CHECK_FALSE(model.world.active.has_value());

    (void)press(model, Key::Kind::ArrowUp);
    CHECK(model.draft == "/create a smith");
}

TEST_CASE("ctrl+enter and ctrl+j start a new line without sending anything") {
    Model model = modelWithNpc();
    type(model, "one");
    const std::vector<Effect> effects = press(model, Key::Kind::CtrlEnter);
    CHECK(model.draft == "one\n");
    CHECK(effects.empty());

    const std::vector<Effect> sent = press(model, Key::Kind::Enter);
    CHECK(hasEffect(sent, EffectKind::Ask));
    REQUIRE(model.messages.size() >= 2);
    CHECK(model.messages.back().text == "one\n");
    CHECK(model.messages.back().role == OpenAnimaTui::App::Role::User);

    Model spaced;
    type(spaced, "two");
    (void)press(spaced, Key::Kind::CtrlJ);
    CHECK(spaced.draft == "two\n");
}

TEST_CASE("a trailing backslash starts a new line instead of sending") {
    Model model = modelWithNpc();
    const std::vector<Effect> effects = submit(model, "one\\");
    CHECK(model.draft == "one\n");
    CHECK(effects.empty());
}

TEST_CASE("one character, not one byte, is removed at a time") {
    Model model;
    type(model, "cañ");
    REQUIRE(model.draft.size() == 4);
    (void)press(model, Key::Kind::Backspace);
    CHECK(model.draft == "ca");

    (void)press(model, Key::Kind::ArrowLeft);
    (void)press(model, Key::Kind::ArrowLeft);
    CHECK(model.cursor == 0);
    (void)press(model, Key::Kind::ArrowRight);
    CHECK(model.cursor == 1);
}

TEST_CASE("a question waits while a turn is in flight") {
    Model model = modelWithNpc();
    (void)submit(model, "hello");
    CHECK(model.busy);

    const std::vector<Effect> effects = submit(model, "later");
    CHECK_FALSE(hasEffect(effects, EffectKind::Ask));
    CHECK(model.draft == "later");
}

TEST_CASE("escape cancels the turn in flight") {
    Model model = modelWithNpc();
    (void)submit(model, "hello");
    const std::vector<Effect> effects = press(model, Key::Kind::Escape);
    CHECK(hasEffect(effects, EffectKind::Cancel));
    CHECK(model.cancelling);

    // The turn is over when the provider says so, not when the key is pressed.
    OpenAnima::Core::Event over;
    over.kind = OpenAnima::Core::EventKind::Finished;
    over.completion.finishReason = OpenAnima::Core::FinishReason::Cancelled;
    over.completion.text = "half an answer";
    (void)OpenAnimaTui::App::applyEvent(model, over);
    CHECK_FALSE(model.busy);
    CHECK_FALSE(model.cancelling);
}

TEST_CASE("ctrl+c is not the cancel key, and does not leave the client") {
    Model model = modelWithNpc();
    (void)submit(model, "hello");

    const std::vector<Effect> effects = press(model, Key::Kind::CtrlC);
    CHECK_FALSE(hasEffect(effects, EffectKind::Cancel));
    CHECK_FALSE(model.cancelling);
    CHECK(model.busy);
    CHECK(model.running);

    // Nor when nothing is in flight: the shell takes the key to copy, never to leave.
    (void)OpenAnimaTui::App::applyEvent(model, finished());
    type(model, "a draft");
    (void)press(model, Key::Kind::CtrlC);
    CHECK(model.draft == "a draft");
    CHECK(model.running);
}

TEST_CASE("the page keys move the viewport and not the caret") {
    Model model;
    (void)press(model, Key::Kind::PageUp);
    CHECK(model.scrollOffset == ScrollStep);
    (void)press(model, Key::Kind::PageDown);
    CHECK(model.scrollOffset == 0);
}

TEST_CASE("the wheel moves the conversation and leaves the prompt alone") {
    Model model;
    type(model, "/create");
    (void)press(model, Key::Kind::ArrowUp);
    const std::string draft = model.draft;

    (void)press(model, Key::Kind::WheelUp);
    CHECK(model.scrollOffset == 1);
    (void)press(model, Key::Kind::WheelUp);
    CHECK(model.scrollOffset == 2);
    (void)press(model, Key::Kind::WheelDown);
    CHECK(model.scrollOffset == 1);

    // A wheel notch is not an arrow: what is typed and what was typed before stay put.
    CHECK(model.draft == draft);
    CHECK(model.hintIndex == 0);

    (void)press(model, Key::Kind::WheelDown);
    (void)press(model, Key::Kind::WheelDown);
    CHECK(model.scrollOffset == 0);
}

TEST_CASE("creating an npc asks the shell for a brain and enters it") {
    Model model;
    const std::vector<Effect> effects = submit(model, "/create a smith");
    CHECK(hasEffect(effects, EffectKind::CreateNpc));
    CHECK(model.world.npcs.find(1) != model.world.npcs.end());
    CHECK(model.world.active == 1);
    CHECK(model.world.npcs.at(1).persona == "a smith");
}

TEST_CASE("every npc keeps the conversation that belongs to it") {
    Model model = modelWithNpc();
    (void)submit(model, "hello");
    answer(model, "well met");
    const std::size_t firstSize = model.messages.size();

    (void)submit(model, "/create a baker");
    CHECK(model.world.active == 2);
    CHECK(model.messages.size() < firstSize);

    (void)submit(model, "/enter 1");
    CHECK(model.world.active == 1);
    CHECK(model.messages.size() == firstSize);
    CHECK(model.messages[model.messages.size() - 1].text == "well met");
    CHECK(model.messages[model.messages.size() - 2].role == OpenAnimaTui::App::Role::User);
}

TEST_CASE("leaving returns to the lobby, which keeps its own log") {
    Model model = modelWithNpc();
    (void)submit(model, "hello");
    answer(model, "well met");
    (void)submit(model, "/leave");
    CHECK_FALSE(model.world.active.has_value());
    CHECK(!model.messages.empty());
    CHECK(model.messages.back().text == "left the conversation");
    for (const Message& message : model.messages) {
        CHECK_FALSE(message.text == "well met");
    }
}

TEST_CASE("an npc that does not exist is reported and not entered") {
    Model model;
    (void)submit(model, "/enter 7");
    CHECK_FALSE(model.world.active.has_value());
    CHECK(!model.messages.empty());
    CHECK(model.world.npcs.empty());
}

TEST_CASE("a failed turn is shown and leaves the prompt usable") {
    Model model = modelWithNpc();
    (void)submit(model, "hello");

    OpenAnima::Core::Event event;
    event.kind = OpenAnima::Core::EventKind::Failed;
    event.error = "no route to host";
    CHECK(OpenAnimaTui::App::applyEvent(model, event));
    CHECK_FALSE(model.busy);
    CHECK(model.answer.empty());
    CHECK(model.messages.back().role == OpenAnimaTui::App::Role::System);
    CHECK(model.messages.back().text.find("no route to host") != std::string::npos);
}

TEST_CASE("the clock only runs while a turn is in flight") {
    Model model = modelWithNpc();
    CHECK(OpenAnimaTui::App::elapsedTenths(model) == 0);
    (void)submit(model, "hello");
    CHECK(OpenAnimaTui::App::elapsedTenths(model) == 0);
    CHECK(model.busy);
    CHECK(model.startedAt != std::chrono::steady_clock::time_point{});
}

TEST_CASE("/raw switches between the dialogue and the document") {
    Model model = modelWithNpc();
    CHECK_FALSE(model.raw);

    (void)submit(model, "/raw");
    CHECK(model.raw);
    CHECK(model.messages.back().text == "raw json on");

    (void)submit(model, "/raw off");
    CHECK_FALSE(model.raw);
    CHECK(model.messages.back().text == "raw json off");

    (void)submit(model, "/raw maybe");
    CHECK_FALSE(model.raw);
    CHECK(model.messages.back().text == "usage: /raw [on|off]");
}

TEST_CASE("raw mode shows the turn as the document, once it is over") {
    Model model = modelWithNpc();
    (void)submit(model, "/raw on");
    (void)submit(model, "hello");

    // Nothing is painted while the turn runs: the document is the only thing that goes in.
    (void)OpenAnimaTui::App::applyEvent(model, delta("Good "));
    CHECK(model.answer.empty());

    (void)OpenAnimaTui::App::applyEvent(
        model,
        finishedWithReply(
            R"({"reasoning":"It looks harmless.","dialogue":"Good evening.","actions":[]})"));

    CHECK_FALSE(model.busy);
    CHECK(model.messages.back().role == OpenAnimaTui::App::Role::System);
    CHECK(model.messages.back().text.find("\"dialogue\": \"Good evening.\"") != std::string::npos);

    // The formatted view is not there: no muted reasoning and no dialogue of its own.
    for (const Message& message : model.messages) {
        CHECK(message.text.rfind("~ ", 0) != 0);
        CHECK(message.text != "Good evening.");
    }
}

TEST_CASE("raw mode says so when there is no document to show") {
    Model model = modelWithNpc();
    (void)submit(model, "/raw on");
    (void)submit(model, "hello");

    OpenAnima::Core::Event over = finished();
    over.completion.finishReason = OpenAnima::Core::FinishReason::Cancelled;
    over.reply = OpenAnima::Core::NpcReply{};
    (void)OpenAnimaTui::App::applyEvent(model, over);

    CHECK_FALSE(model.busy);
    CHECK(model.messages.back().text.find("no document") != std::string::npos);
}

TEST_CASE("a reply is one block, named on the thought that opens it") {
    Model model = modelWithNpc();
    (void)submit(model, "hello");
    (void)OpenAnimaTui::App::applyEvent(model, delta("Good evening."));
    (void)OpenAnimaTui::App::applyEvent(
        model, finishedWithReply(R"({"reasoning":"It looks harmless.","dialogue":"Good evening.",)"
                                 R"("actions":[{"name":"emote","arguments":{"kind":"nod"}}]})"));

    const std::size_t count = model.messages.size();
    REQUIRE(count >= 3);

    // The thought opens the block and carries the name; the rest hangs from it.
    CHECK(model.messages[count - 3].role == OpenAnimaTui::App::Role::Thought);
    CHECK(model.messages[count - 3].text.find("It looks harmless.") != std::string::npos);
    CHECK_FALSE(model.messages[count - 3].continues);

    CHECK(model.messages[count - 2].role == OpenAnimaTui::App::Role::Npc);
    CHECK(model.messages[count - 2].text == "Good evening.");
    CHECK(model.messages[count - 2].continues);

    CHECK(model.messages[count - 1].role == OpenAnimaTui::App::Role::System);
    CHECK(model.messages[count - 1].text.rfind("[emote]", 0) == 0);
    CHECK(model.messages[count - 1].continues);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
