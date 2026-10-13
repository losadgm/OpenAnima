/// @file
/// @brief What the screen looks like: panels, hints, viewport and caret.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "OpenAnima/Core/Event.h"

#include "app/model.h"
#include "screen/render.h"
#include "screen/style.h"
#include "screen/text.h"

namespace {

using OpenAnimaTui::Key;
using OpenAnimaTui::App::Model;
using OpenAnimaTui::Screen::render;
using OpenAnimaTui::Screen::RenderResult;
using OpenAnimaTui::Screen::visibleColumns;

constexpr std::size_t ScrollStep = 5;

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            return lines;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

void type(Model& model, const std::string& text) {
    for (const char value : text) {
        (void)OpenAnimaTui::App::update(model, Key{Key::Kind::Character, value}, ScrollStep);
    }
}

std::vector<OpenAnimaTui::App::Effect> press(Model& model, const Key::Kind kind) {
    return OpenAnimaTui::App::update(model, Key{kind, 0}, ScrollStep);
}

void submit(Model& model, const std::string& text) {
    type(model, text);
    (void)press(model, Key::Kind::Enter);
}

void answer(Model& model, const std::string& text) {
    OpenAnima::Core::Event event;
    event.kind = OpenAnima::Core::EventKind::Finished;
    event.completion.text = text;
    event.completion.finishReason = OpenAnima::Core::FinishReason::Stop;
    (void)OpenAnimaTui::App::update(model, Key{Key::Kind::None, 0}, ScrollStep);
    OpenAnima::Core::Event piece;
    piece.kind = OpenAnima::Core::EventKind::Delta;
    piece.text = text;
    (void)OpenAnimaTui::App::applyEvent(model, piece);
    (void)OpenAnimaTui::App::applyEvent(model, event);
}

/// A conversation with eight turns in it, so that it does not fit on a short screen.
Model longConversation() {
    Model model;
    submit(model, "/create a smith");
    for (int turn = 0; turn < 8; ++turn) {
        submit(model, "m" + std::to_string(turn));
        answer(model, "a" + std::to_string(turn));
    }
    return model;
}

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("every row is exactly as wide as the terminal") {
    Model model;
    type(model, "hello");
    const RenderResult result = render(model, 24, 6);
    for (const std::string& line : splitLines(result.text)) {
        CHECK(visibleColumns(line) == 24);
    }
}

TEST_CASE("the prompt floats on its own panel, with the caret in it") {
    Model model;
    type(model, "cañ");
    const RenderResult result = render(model, 40, 10);
    const std::vector<std::string> lines = splitLines(result.text);

    REQUIRE(lines.size() == 10);
    CHECK(lines.back().find(OpenAnimaTui::Screen::Panel) == 0);
    CHECK(lines.back().find(OpenAnimaTui::Screen::PromptMarker) != std::string::npos);
    CHECK(result.caretRow == 9);
    CHECK(result.caretCol == 2 + 3);
}

TEST_CASE("a long prompt wraps and the caret follows it down") {
    Model model;
    type(model, std::string(30, 'x'));
    const RenderResult result = render(model, 20, 12);
    CHECK(result.caretRow == 11);
    CHECK(result.caretCol == 2 + (30 - (20 - 2)));
}

TEST_CASE("the conversation is a viewport over the turns") {
    Model model = longConversation();
    const std::string full = render(model, 40, 100).text;
    CHECK(full.find("m0") != std::string::npos);
    CHECK(full.find("a7") != std::string::npos);

    const std::string tail = render(model, 40, 8).text;
    CHECK(tail.find("m0") == std::string::npos);
    CHECK(tail.find("a7") != std::string::npos);

    (void)press(model, Key::Kind::PageUp);
    const std::string scrolled = render(model, 40, 8).text;
    CHECK(scrolled.find("m3") != std::string::npos);
    CHECK(scrolled.find("a7") == std::string::npos);

    for (int step = 0; step < 2; ++step) {
        (void)press(model, Key::Kind::PageUp);
    }
    const std::string top = render(model, 40, 8).text;
    CHECK(top.find("m0") != std::string::npos);
    CHECK(top.find("a4") == std::string::npos);
}

TEST_CASE("the hints float over the conversation and the selected one stands out") {
    Model model;
    type(model, "/");
    RenderResult result = render(model, 40, 12);

    std::vector<std::string> hintRows;
    for (const std::string& line : splitLines(result.text)) {
        if (line.rfind(OpenAnimaTui::Screen::Raised, 0) == 0) {
            hintRows.push_back(line);
        }
    }
    REQUIRE(hintRows.size() == OpenAnimaTui::Screen::MaxHints);
    CHECK(hintRows[0].find(std::string{OpenAnimaTui::Screen::Accent} + "/create") !=
          std::string::npos);
    CHECK(hintRows[1].find(std::string{OpenAnimaTui::Screen::Accent} + "/enter") ==
          std::string::npos);

    (void)press(model, Key::Kind::ArrowDown);
    result = render(model, 40, 12);
    hintRows.clear();
    for (const std::string& line : splitLines(result.text)) {
        if (line.rfind(OpenAnimaTui::Screen::Raised, 0) == 0) {
            hintRows.push_back(line);
        }
    }
    REQUIRE(hintRows.size() == OpenAnimaTui::Screen::MaxHints);
    CHECK(hintRows[1].find(std::string{OpenAnimaTui::Screen::Accent} + "/enter") !=
          std::string::npos);
    CHECK(hintRows[0].find(std::string{OpenAnimaTui::Screen::Accent} + "/create") ==
          std::string::npos);
}

TEST_CASE("the status row changes once there is an npc to talk to") {
    Model model;
    CHECK(render(model, 40, 10).text.find("no npc") != std::string::npos);
    submit(model, "/create a smith");
    CHECK(render(model, 40, 10).text.find("ask something") != std::string::npos);
}

TEST_CASE("a note from the world is indented and quiet") {
    Model model;
    submit(model, "/create a smith");
    const std::string text = render(model, 40, 10).text;
    CHECK(text.find(std::string{OpenAnimaTui::Screen::Muted} + std::string(8, ' ') + "created") !=
          std::string::npos);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
