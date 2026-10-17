/// @file
/// @brief What the client shows: the conversation, the hints, the status and the prompt.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <vector>

#include "OpenAnima/Core/Event.h"

#include "app/commands.h"
#include "app/model.h"
#include "screen/render.h"
#include "screen/selection.h"
#include "screen/text.h"
#include "tui_test_support.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

namespace {

using OpenAnimaTui::Key;
using OpenAnimaTui::App::Effect;
using OpenAnimaTui::App::Model;
using OpenAnimaTui::Screen::render;
using OpenAnimaTui::Screen::RenderResult;
using OpenAnimaTui::Screen::Selection;
using OpenAnimaTui::Screen::Metrics::MaxHints;
using namespace OpenAnimaTui::Tests;

constexpr std::size_t ScrollStep = 5;

void type(Model& model, const std::string& text) {
    for (const char value : text) {
        (void)OpenAnimaTui::App::update(model, Key{Key::Kind::Character, value}, ScrollStep);
    }
}

std::vector<Effect> press(Model& model, const Key::Kind kind) {
    return OpenAnimaTui::App::update(model, Key{kind, 0}, ScrollStep);
}

void submit(Model& model, const std::string& text) {
    type(model, text);
    (void)press(model, Key::Kind::Enter);
}

void answer(Model& model, const std::string& text) {
    OpenAnima::Core::Event piece;
    piece.kind = OpenAnima::Core::EventKind::Delta;
    piece.text = text;
    (void)OpenAnimaTui::App::applyEvent(model, piece);

    OpenAnima::Core::Event done;
    done.kind = OpenAnima::Core::EventKind::Finished;
    done.completion.text = text;
    done.completion.finishReason = OpenAnima::Core::FinishReason::Stop;
    (void)OpenAnimaTui::App::applyEvent(model, done);
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

/// The rows of the screen painted over the raised panel.
std::vector<std::size_t> raisedRows(const RenderResult& rendered) {
    std::vector<std::size_t> rows;
    for (std::size_t row = 0; row < rendered.frame.height(); ++row) {
        if (rendered.frame.at(row, 0).style.background.equals(Palette::raisedColor())) {
            rows.push_back(row);
        }
    }
    return rows;
}

/// Whether the cell holds the stroke the scrollbar is drawn with.
bool isBarCell(const Cell& cell) {
    return !cell.isBlank() &&
           std::string(cell.glyph.data(), cell.length) == OpenAnimaTui::Screen::Glyphs::ScrollBar;
}

/// @return How many rows the conversation occupies: everything above the frame of the prompt,
///         whose top border starts with its upper left corner.
std::size_t viewportRows(const RenderResult& rendered) {
    for (std::size_t row = 0; row < rendered.frame.height(); ++row) {
        if (readRow(rendered.frame, row).rfind("\U0001FB7D", 0) == 0) {
            return row;
        }
    }
    return rendered.frame.height();
}

/// The rows where the scrollbar is shown at all, read off the last column.
std::vector<std::size_t> barRows(const RenderResult& rendered) {
    std::vector<std::size_t> rows;
    const std::size_t column = rendered.frame.width() - 1;
    const std::size_t last = viewportRows(rendered);
    for (std::size_t row = 0; row < last; ++row) {
        if (isBarCell(rendered.frame.at(row, column))) {
            rows.push_back(row);
        }
    }
    return rows;
}

/// The rows where the scrollbar shows the thumb, which is the part that is not dimmed.
std::vector<std::size_t> thumbRows(const RenderResult& rendered) {
    std::vector<std::size_t> rows;
    const std::size_t column = rendered.frame.width() - 1;
    const std::size_t last = viewportRows(rendered);
    for (std::size_t row = 0; row < last; ++row) {
        const Cell& cell = rendered.frame.at(row, column);
        if (isBarCell(cell) && !cell.style.dim) {
            rows.push_back(row);
        }
    }
    return rows;
}

}  // namespace

TEST_CASE("no row of the screen is wider than the terminal") {
    Model model;
    type(model, "hello");
    const RenderResult rendered = render(model, 24, 6);
    CHECK(rendered.frame.width() == 24);
    CHECK(rendered.frame.height() == 6);
    for (std::size_t row = 0; row < rendered.frame.height(); ++row) {
        CHECK(OpenAnimaTui::Screen::measure(readRow(rendered.frame, row)) == 24);
    }
}

TEST_CASE("the prompt sits inside a frame, with the caret in it") {
    Model model;
    type(model, "ca\u00f1");
    const RenderResult rendered = render(model, 40, 10);

    // Ten rows: six of conversation, the top border, the line, the bottom border and the status.
    CHECK(readRow(rendered.frame, 6).rfind("\U0001FB7D", 0) == 0);
    CHECK(readRow(rendered.frame, 8).rfind("\U0001FB7C", 0) == 0);

    // The panel is the box, the border rows included, and the strokes are painted on it.
    CHECK(rendered.frame.at(6, 0).style.background.equals(Palette::panelColor()));
    CHECK(rendered.frame.at(7, 0).style.background.equals(Palette::panelColor()));
    CHECK(rendered.frame.at(7, 1).style.background.equals(Palette::panelColor()));
    CHECK(rendered.frame.at(9, 0).style.background.kind == Color::Kind::Default);

    const OpenAnimaTui::Screen::Cell& marker = rendered.frame.at(7, 2);
    CHECK(std::string(marker.glyph.data(), marker.length) == "\u2771");
    CHECK(marker.style.foreground.equals(Palette::accent().foreground));
    CHECK(marker.style.background.equals(Palette::panelColor()));
    CHECK(rendered.caretRow == 7);
    CHECK(rendered.caretColumn == 4 + 3);
}

TEST_CASE("the status line sits under the frame, with no indent and no panel") {
    Model model;
    const RenderResult rendered = render(model, 40, 10);

    CHECK(readRow(rendered.frame, 9).rfind("no npc", 0) == 0);
    CHECK(rendered.frame.at(9, 0).style.background.kind == Color::Kind::Default);
    CHECK(readRow(rendered.frame, 8).rfind("\U0001FB7C", 0) == 0);
}

TEST_CASE("the empty prompt says what can be done, and stops saying it when typed in") {
    Model model;
    CHECK(readAll(render(model, 40, 10).frame).find("/create <description> to make an npc") !=
          std::string::npos);

    submit(model, "/create a smith");
    CHECK(readAll(render(model, 40, 10).frame).find("say something, or /help") !=
          std::string::npos);

    type(model, "hola");
    const std::string typed = readAll(render(model, 40, 10).frame);
    CHECK(typed.find("say something") == std::string::npos);
    CHECK(typed.find("hola") != std::string::npos);
}

TEST_CASE("a long prompt wraps and the caret follows it down") {
    Model model;
    type(model, std::string(30, 'x'));
    const RenderResult rendered = render(model, 20, 12);
    // Inside the box: the stroke, a column of panel, the marker and its space, fourteen of text.
    CHECK(rendered.caretRow == 9);
    CHECK(rendered.caretColumn == 4 + (30 - 28));
}

TEST_CASE("the conversation is a viewport over the turns") {
    Model model = longConversation();
    CHECK(readAll(render(model, 40, 100).frame).find("m0") != std::string::npos);
    CHECK(readAll(render(model, 40, 100).frame).find("a7") != std::string::npos);

    // Ten rows leave six for the conversation: the frame takes three and the status one.
    const std::string tail = readAll(render(model, 40, 10).frame);
    CHECK(tail.find("m0") == std::string::npos);
    CHECK(tail.find("a7") != std::string::npos);

    (void)press(model, Key::Kind::PageUp);
    const std::string scrolled = readAll(render(model, 40, 10).frame);
    CHECK(scrolled.find("m6") != std::string::npos);
    CHECK(scrolled.find("a7") == std::string::npos);

    for (int step = 0; step < 6; ++step) {
        (void)press(model, Key::Kind::PageUp);
    }
    const std::string top = readAll(render(model, 40, 10).frame);
    CHECK(top.find("m0") != std::string::npos);
    CHECK(top.find("a4") == std::string::npos);
}

TEST_CASE("a thin bar on the right shows where the viewport sits") {
    Model model = longConversation();

    RenderResult rendered = render(model, 40, 12);
    REQUIRE(rendered.maxScrollOffset > 0);

    // Pinned to the bottom, the thumb ends where the viewport ends.
    std::vector<std::size_t> track = barRows(rendered);
    std::vector<std::size_t> thumb = thumbRows(rendered);
    REQUIRE_FALSE(track.empty());
    REQUIRE_FALSE(thumb.empty());
    CHECK(thumb.back() == track.back());
    CHECK(thumb.front() == thumb.back() - thumb.size() + 1);

    // Scrolled all the way up, it sits at the top instead.
    for (int step = 0; step < 8; ++step) {
        (void)press(model, Key::Kind::PageUp);
    }
    rendered = render(model, 40, 12);
    track = barRows(rendered);
    thumb = thumbRows(rendered);
    REQUIRE_FALSE(track.empty());
    REQUIRE_FALSE(thumb.empty());
    CHECK(thumb.front() == track.front());
}

TEST_CASE("the hints float over the conversation and the selected one stands out") {
    Model model;
    type(model, "/");
    RenderResult rendered = render(model, 40, 12);

    std::vector<std::size_t> rows = raisedRows(rendered);
    REQUIRE(rows.size() == MaxHints);
    CHECK(readRow(rendered.frame, rows[0]).find("/create") != std::string::npos);
    CHECK(rendered.frame.at(rows[0], 2).style.foreground.equals(Palette::accent().foreground));
    CHECK(rendered.frame.at(rows[1], 2).style.foreground.equals(Palette::muted().foreground));

    (void)press(model, Key::Kind::ArrowDown);
    rendered = render(model, 40, 12);
    rows = raisedRows(rendered);
    REQUIRE(rows.size() == MaxHints);
    CHECK(rendered.frame.at(rows[1], 2).style.foreground.equals(Palette::accent().foreground));
    CHECK(rendered.frame.at(rows[0], 2).style.foreground.equals(Palette::muted().foreground));
}

TEST_CASE("the hint panel follows the selection when the list does not fit") {
    Model model;
    type(model, "/");
    REQUIRE(OpenAnimaTui::App::commands().size() > MaxHints);

    // Walk to the last command, which used to be cut off the bottom of the panel.
    for (std::size_t step = 0; step + 1 < OpenAnimaTui::App::commands().size(); ++step) {
        (void)press(model, Key::Kind::ArrowDown);
    }
    const RenderResult rendered = render(model, 40, 12);
    const std::vector<std::size_t> rows = raisedRows(rendered);

    REQUIRE(rows.size() == MaxHints);
    CHECK(readRow(rendered.frame, rows.back()).find("/quit") != std::string::npos);
    CHECK(rendered.frame.at(rows.back(), 2).style.foreground.equals(Palette::accent().foreground));
}

TEST_CASE("the status row changes once there is an npc to talk to") {
    Model model;
    CHECK(readAll(render(model, 40, 10).frame).find("no npc") != std::string::npos);
    submit(model, "/create a smith");
    CHECK(readAll(render(model, 40, 10).frame).find("ask something") != std::string::npos);
}

TEST_CASE("a note from the world is indented and quiet") {
    Model model;
    submit(model, "/create a smith");
    const RenderResult rendered = render(model, 40, 10);
    CHECK(readRow(rendered.frame, 0).substr(0, 8) == "        ");
    CHECK(readRow(rendered.frame, 0).find("created npc 1") != std::string::npos);
    CHECK(rendered.frame.at(0, 8).style.equals(Palette::muted()));
}

TEST_CASE("the speaker of a line is named in the prefix and the text follows it") {
    Model model;
    submit(model, "/create a smith");
    submit(model, "hello there");
    answer(model, "well met");
    const RenderResult rendered = render(model, 40, 12);

    CHECK(readRow(rendered.frame, 2).substr(0, 8) == "you     ");
    CHECK(readRow(rendered.frame, 2).substr(8).find("hello there") == 0);
    // The blank rows that keep the note, the prompt and the answer apart.
    CHECK(readRow(rendered.frame, 1) == std::string(40, ' '));
    CHECK(readRow(rendered.frame, 3) == std::string(40, ' '));
    CHECK(readRow(rendered.frame, 4).substr(0, 8) == "npc 1   ");
    CHECK(readRow(rendered.frame, 4).substr(8).find("well met") == 0);
    CHECK(rendered.frame.at(4, 0).style.foreground.equals(Palette::primary().foreground));
    CHECK(rendered.frame.at(4, 8).style.foreground.equals(Palette::text().foreground));
}

TEST_CASE("a reply is painted as one block, named on its first row") {
    Model model;
    submit(model, "/create a smith");
    submit(model, "hello");

    OpenAnima::Core::Event piece;
    piece.kind = OpenAnima::Core::EventKind::Delta;
    piece.text = "Good evening.";
    (void)OpenAnimaTui::App::applyEvent(model, piece);

    OpenAnima::Core::Event done;
    done.kind = OpenAnima::Core::EventKind::Finished;
    done.completion.finishReason = OpenAnima::Core::FinishReason::Stop;
    OpenAnima::Core::NpcReply reply;
    reply.reasoning = "It looks harmless.";
    reply.dialogue = "Good evening.";
    reply.actions.push_back({"emote", nlohmann::ordered_json{{"kind", "nod"}}});
    done.reply = reply;
    (void)OpenAnimaTui::App::applyEvent(model, done);

    const RenderResult rendered = render(model, 60, 13);

    // The note, the question, and then the answer as one block with no gaps in it.
    CHECK(readRow(rendered.frame, 4).substr(0, 8) == "npc 1   ");
    CHECK(readRow(rendered.frame, 4).substr(8).find("~ It looks harmless.") == 0);
    CHECK(rendered.frame.at(4, 8).style.foreground.equals(Palette::muted().foreground));
    CHECK(readRow(rendered.frame, 5).substr(0, 8) == "        ");
    CHECK(readRow(rendered.frame, 5).substr(8).find("Good evening.") == 0);
    CHECK(rendered.frame.at(5, 8).style.foreground.equals(Palette::text().foreground));
    CHECK(readRow(rendered.frame, 6).substr(0, 8) == "        ");
    CHECK(readRow(rendered.frame, 6).substr(8).find("[emote]") == 0);
}

TEST_CASE("a character still arriving leaves the prompt clean") {
    Model model;
    type(model, "a");
    type(model, "\xc3");  // The first byte of an enye, with the one after it still on its way.
    RenderResult rendered = render(model, 40, 10);
    CHECK(readRow(rendered.frame, 7).rfind("\u258F \u2771 a", 0) == 0);
    CHECK(rendered.frame.at(7, 5).isBlank());

    type(model, "\xb1");
    rendered = render(model, 40, 10);
    CHECK(readRow(rendered.frame, 7).find("a\u00f1") != std::string::npos);
    CHECK(rendered.frame.at(7, 5).length == 2);
}

TEST_CASE("a turn in flight is shown as the answer arrives") {
    Model model;
    submit(model, "/create a smith");
    submit(model, "hello");
    OpenAnima::Core::Event piece;
    piece.kind = OpenAnima::Core::EventKind::Delta;
    piece.text = "waiting";
    (void)OpenAnimaTui::App::applyEvent(model, piece);

    const std::string screen = readAll(render(model, 40, 12).frame);
    CHECK(screen.find("waiting") != std::string::npos);
    CHECK(screen.find("working") != std::string::npos);
}

TEST_CASE("what the mouse is holding is painted, down to the cell it went down on") {
    Model model;
    type(model, "hola");

    Selection selection;
    selection.begin(9, 2);
    selection.extend(9, 5);
    RenderResult rendered = render(model, 40, 10, selection);
    CHECK(rendered.frame.at(9, 2).style.reverse);
    CHECK(rendered.frame.at(9, 5).style.reverse);
    CHECK_FALSE(rendered.frame.at(9, 1).style.reverse);
    CHECK_FALSE(rendered.frame.at(9, 6).style.reverse);

    // A drag that comes back over where the button went down is still a drag, and the one
    // cell it covers is still painted.
    selection.extend(9, 2);
    rendered = render(model, 40, 10, selection);
    CHECK(rendered.frame.at(9, 2).style.reverse);
    CHECK_FALSE(rendered.frame.at(9, 3).style.reverse);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
