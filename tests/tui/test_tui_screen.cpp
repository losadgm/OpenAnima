/// @file
/// @brief The screen logic of the terminal client, with the terminal replaced by a width.

#include <catch2/catch_test_macros.hpp>
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

OpenAnimaTui::Key characterKey(char character) {
    return {OpenAnimaTui::Key::Kind::Character, character};
}

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("wrap breaks at spaces and honours newlines") {
    CHECK(OpenAnimaTui::wrap("one two three", 8) == std::vector<std::string>{"one two", "three"});
    CHECK(OpenAnimaTui::wrap("one\ntwo", 10) == std::vector<std::string>{"one", "two"});
    CHECK(OpenAnimaTui::wrap("a verylongword", 5) ==
          std::vector<std::string>{"a", "veryl", "ongwo", "rd"});
}

TEST_CASE("wrap drops a trailing empty line") {
    CHECK(OpenAnimaTui::wrap("one\n", 10) == std::vector<std::string>{"one"});
    CHECK(OpenAnimaTui::wrap("", 10) == std::vector<std::string>{""});
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
    CHECK(npc[0].find("\x1b[32m") != std::string::npos);
    CHECK(user[0].find("\x1b[34m") != std::string::npos);
}

TEST_CASE("renderFrame draws the quiet line and the prompt when idle") {
    OpenAnimaTui::ScreenState state;
    state.model = "mock";
    state.hint = "ask something";
    state.draft = "hello";

    const std::string frame = OpenAnimaTui::renderFrame(state, 40);
    const std::string plain = stripAnsi(frame);
    CHECK(plain == "        mock   ask something\n> hello");
}

TEST_CASE("renderFrame draws the answer and the working line while busy") {
    OpenAnimaTui::ScreenState state;
    state.busy = true;
    state.answer = "Iron?";
    state.startedAt = std::chrono::steady_clock::now();

    const std::string frame = OpenAnimaTui::renderFrame(state, 40);
    const std::string plain = stripAnsi(frame);
    CHECK(plain.find("Aldric  Iron?") == 0);
    CHECK(plain.find("\n        working 0.0s   esc to cancel\n> ") != std::string::npos);
}

TEST_CASE("renderFrame wraps a long draft into more rows") {
    OpenAnimaTui::ScreenState state;
    state.draft = "a fairly long question that does not fit in one row";

    const std::string frame = OpenAnimaTui::renderFrame(state, 20);
    const std::string plain = stripAnsi(frame);
    CHECK(plain == "           \n> a fairly long ques\n  tion that does not\n   fit in one row");
}

TEST_CASE("caretOffset is zero at the end of the draft") {
    OpenAnimaTui::ScreenState state;
    state.draft = "hello";
    state.cursor = 5;

    const auto [up, left] = OpenAnimaTui::caretOffset(state, 40);
    CHECK(up == 0);
    CHECK(left == 0);
}

TEST_CASE("caretOffset walks back along a wrapped draft") {
    OpenAnimaTui::ScreenState state;
    state.draft = "abcdefghij";
    state.cursor = 1;

    const auto [up, left] = OpenAnimaTui::caretOffset(state, 10);
    CHECK(up == 1);
    CHECK(left == 1);
}

TEST_CASE("caretOffset reaches the start of a multiline draft") {
    OpenAnimaTui::ScreenState state;
    state.draft = "one\ntwo";
    state.cursor = 0;

    const auto [up, left] = OpenAnimaTui::caretOffset(state, 40);
    CHECK(up == 1);
    CHECK(left == 3);
}

TEST_CASE("mapKey interrupts with escape or ctrl+c while busy") {
    const OpenAnimaTui::Key escape{OpenAnimaTui::Key::Kind::Escape, static_cast<char>(0)};
    const OpenAnimaTui::Key ctrlC{OpenAnimaTui::Key::Kind::CtrlC, static_cast<char>(0)};
    CHECK(OpenAnimaTui::mapKey(escape, true, false) == OpenAnimaTui::Action::Cancel);
    CHECK(OpenAnimaTui::mapKey(ctrlC, true, false) == OpenAnimaTui::Action::Cancel);
}

TEST_CASE("mapKey turns ctrl+c into clear or leave depending on the draft") {
    const OpenAnimaTui::Key ctrlC{OpenAnimaTui::Key::Kind::CtrlC, static_cast<char>(0)};
    CHECK(OpenAnimaTui::mapKey(ctrlC, false, false) == OpenAnimaTui::Action::ClearDraft);
    CHECK(OpenAnimaTui::mapKey(ctrlC, false, true) == OpenAnimaTui::Action::Leave);
}

TEST_CASE("mapKey decodes navigation and editing keys") {
    CHECK(OpenAnimaTui::mapKey(characterKey('a'), false, true) == OpenAnimaTui::Action::Insert);
    CHECK(OpenAnimaTui::mapKey({OpenAnimaTui::Key::Kind::ArrowUp, static_cast<char>(0)}, false,
                               true) == OpenAnimaTui::Action::HistoryPrevious);
    CHECK(OpenAnimaTui::mapKey({OpenAnimaTui::Key::Kind::ArrowDown, static_cast<char>(0)}, false,
                               true) == OpenAnimaTui::Action::HistoryNext);
    CHECK(OpenAnimaTui::mapKey({OpenAnimaTui::Key::Kind::CtrlA, static_cast<char>(0)}, false,
                               true) == OpenAnimaTui::Action::MoveHome);
    CHECK(OpenAnimaTui::mapKey({OpenAnimaTui::Key::Kind::CtrlE, static_cast<char>(0)}, false,
                               true) == OpenAnimaTui::Action::MoveEnd);
    CHECK(OpenAnimaTui::mapKey({OpenAnimaTui::Key::Kind::CtrlD, static_cast<char>(0)}, false,
                               true) == OpenAnimaTui::Action::Leave);
    CHECK(OpenAnimaTui::mapKey({OpenAnimaTui::Key::Kind::CtrlJ, static_cast<char>(0)}, false,
                               true) == OpenAnimaTui::Action::Newline);
}

TEST_CASE("elapsedTenths is zero when nothing is running") {
    OpenAnimaTui::ScreenState state;
    CHECK(OpenAnimaTui::elapsedTenths(state) == 0);
    state.busy = true;
    state.startedAt = std::chrono::steady_clock::now();
    CHECK(OpenAnimaTui::elapsedTenths(state) == 0);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
