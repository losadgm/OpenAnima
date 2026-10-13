/// @file
/// @brief The screen logic of the terminal client, with the terminal replaced by a size.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <vector>

#include "screen.h"

namespace {

/// Strips the ANSI SGR colour sequences so the tests compare words, not paint.
std::string stripAnsi(const std::string& text) {
    std::string plain;
    plain.reserve(text.size());
    std::size_t index = 0;
    while (index < text.size()) {
        if (text[index] == '\x1b' && index + 1 < text.size() && text[index + 1] == '[') {
            index += 2;
            while (index < text.size() && text[index] != 'm') {
                ++index;
            }
            if (index < text.size()) {
                ++index;
            }
            continue;
        }
        plain += text[index];
        ++index;
    }
    return plain;
}

OpenAnimaTui::Key keyOf(OpenAnimaTui::Key::Kind kind) {
    return {kind, static_cast<char>(0)};
}

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("wrap breaks at spaces and honours newlines") {
    CHECK(OpenAnimaTui::wrap("one two three", 8) == std::vector<std::string>{"one two", "three"});
    CHECK(OpenAnimaTui::wrap("one\ntwo", 10) == std::vector<std::string>{"one", "two"});
    CHECK(OpenAnimaTui::wrap("a verylongword", 5) ==
          std::vector<std::string>{"a", "veryl", "ongwo", "rd"});
}

TEST_CASE("wrap does not split a multi-byte character") {
    CHECK(OpenAnimaTui::wrap("cañón", 3) == std::vector<std::string>{"cañ", "ón"});
}

TEST_CASE("renderLine puts the speaker first and indents the rest") {
    const std::vector<std::string> lines =
        OpenAnimaTui::renderLine("Aldric", "three of them", 20, OpenAnimaTui::Role::Npc);
    REQUIRE(lines.size() == 2);
    CHECK(stripAnsi(lines[0]) == "Aldric  three of");
    CHECK(stripAnsi(lines[1]) == "        them");
}

TEST_CASE("renderLine colours the role") {
    const std::vector<std::string> npc =
        OpenAnimaTui::renderLine("Aldric", "a", 20, OpenAnimaTui::Role::Npc);
    const std::vector<std::string> user =
        OpenAnimaTui::renderLine("you", "a", 20, OpenAnimaTui::Role::User);
    CHECK(npc[0].find("\x1b[38;2;250;178;131m") != std::string::npos);   // primary #fab283
    CHECK(user[0].find("\x1b[38;2;128;128;128m") != std::string::npos);  // muted #808080
}

TEST_CASE("renderScreen puts the prompt at the bottom with the caret in it") {
    OpenAnimaTui::ScreenState state;
    state.model = "mock";
    state.hint = "ask";
    state.draft = "hi";
    state.cursor = 2;

    const OpenAnimaTui::RenderResult result = OpenAnimaTui::renderScreen(state, 40, 10);
    CHECK(stripAnsi(result.text).find("\u2771 hi") != std::string::npos);
    CHECK(result.caretRow == 9);
    CHECK(result.caretCol == 4);
}

TEST_CASE("renderScreen shows a message at the top of the viewport") {
    OpenAnimaTui::ScreenState state;
    state.messages = {{OpenAnimaTui::Role::User, "hello"}};

    const OpenAnimaTui::RenderResult result = OpenAnimaTui::renderScreen(state, 20, 5);
    CHECK(stripAnsi(result.text).find("you     hello") == 0);
}

TEST_CASE("renderScreen shows a system note indented") {
    OpenAnimaTui::ScreenState state;
    state.messages = {{OpenAnimaTui::Role::System, "a note"}};

    const OpenAnimaTui::RenderResult result = OpenAnimaTui::renderScreen(state, 20, 5);
    CHECK(stripAnsi(result.text).find("        a note") != std::string::npos);
}

TEST_CASE("renderScreen scrolls the transcript") {
    OpenAnimaTui::ScreenState state;
    for (int index = 0; index < 5; ++index) {
        state.messages.push_back({OpenAnimaTui::Role::User, "m" + std::to_string(index)});
    }

    const OpenAnimaTui::RenderResult bottom = OpenAnimaTui::renderScreen(state, 20, 6);
    CHECK(stripAnsi(bottom.text).find("you     m1") == 0);

    state.scrollOffset = 1;
    const OpenAnimaTui::RenderResult scrolled = OpenAnimaTui::renderScreen(state, 20, 6);
    CHECK(stripAnsi(scrolled.text).find("you     m0") == 0);
}

TEST_CASE("renderScreen colours the command token in the prompt") {
    OpenAnimaTui::ScreenState state;
    state.draft = "/create a smith";
    state.cursor = 15;

    const OpenAnimaTui::RenderResult result = OpenAnimaTui::renderScreen(state, 40, 10);
    CHECK(result.text.find("\x1b[38;2;157;124;216m/create\x1b[0m") != std::string::npos);
}

TEST_CASE("renderScreen floats the matching commands above the prompt") {
    OpenAnimaTui::ScreenState state;
    state.draft = "/c";
    state.cursor = 2;

    const OpenAnimaTui::RenderResult result = OpenAnimaTui::renderScreen(state, 40, 10);
    CHECK(stripAnsi(result.text).find("  /create") != std::string::npos);
}

TEST_CASE("commandMatches finds what completes the typed prefix") {
    CHECK(OpenAnimaTui::commandMatches("/", 1).size() == OpenAnimaTui::commands().size());
    CHECK(OpenAnimaTui::commandMatches("/c", 2) == std::vector<std::string>{"/create"});
    CHECK(OpenAnimaTui::commandMatches("/h", 2) ==
          std::vector<std::string>{"/help", "/history clear"});
    CHECK(OpenAnimaTui::commandMatches("/create a", 8).empty());
    CHECK(OpenAnimaTui::commandMatches("hello", 5).empty());
}

TEST_CASE("completeCommand completes the single match and the common prefix") {
    const auto [single, singleCursor] = OpenAnimaTui::completeCommand("/c", 2);
    CHECK(single == "/create ");
    CHECK(singleCursor == 8);

    const auto [many, manyCursor] = OpenAnimaTui::completeCommand("/h", 2);
    CHECK(many == "/h");
    CHECK(manyCursor == 2);

    const auto [full, fullCursor] = OpenAnimaTui::completeCommand("/hel", 4);
    CHECK(full == "/help ");
    CHECK(fullCursor == 6);
}

TEST_CASE("mapKey completes with tab inside a command and inserts it elsewhere") {
    CHECK(OpenAnimaTui::mapKey(keyOf(OpenAnimaTui::Key::Kind::Tab), false, false, true) ==
          OpenAnimaTui::Action::Complete);
    CHECK(OpenAnimaTui::mapKey(keyOf(OpenAnimaTui::Key::Kind::Tab), false, false, false) ==
          OpenAnimaTui::Action::Insert);
}

TEST_CASE("mapKey scrolls with page up and page down") {
    CHECK(OpenAnimaTui::mapKey(keyOf(OpenAnimaTui::Key::Kind::PageUp), false, false, false) ==
          OpenAnimaTui::Action::ScrollUp);
    CHECK(OpenAnimaTui::mapKey(keyOf(OpenAnimaTui::Key::Kind::PageDown), false, false, false) ==
          OpenAnimaTui::Action::ScrollDown);
}

TEST_CASE("mapKey turns ctrl+c into clear or leave depending on the draft") {
    const OpenAnimaTui::Key ctrlC = keyOf(OpenAnimaTui::Key::Kind::CtrlC);
    CHECK(OpenAnimaTui::mapKey(ctrlC, false, false, false) == OpenAnimaTui::Action::ClearDraft);
    CHECK(OpenAnimaTui::mapKey(ctrlC, false, true, false) == OpenAnimaTui::Action::Leave);
}

TEST_CASE("elapsedTenths is zero when nothing is running") {
    OpenAnimaTui::ScreenState state;
    CHECK(OpenAnimaTui::elapsedTenths(state) == 0);
    state.busy = true;
    state.startedAt = std::chrono::steady_clock::now();
    CHECK(OpenAnimaTui::elapsedTenths(state) == 0);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
