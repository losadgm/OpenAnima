/// @file
/// @brief What the pointer does to the screen: picks, drags, scrolls and copies.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "application/interaction.h"
#include "domain/model.h"
#include "presentation/render.h"
#include "presentation/selection.h"
#include "terminal/terminal.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

namespace {

using OpenAnimaTui::Key;
using OpenAnimaTui::Terminal;
using OpenAnimaTui::Application::applyKey;
using OpenAnimaTui::Application::autoscrollAtEdge;
using OpenAnimaTui::Application::handleKey;
using OpenAnimaTui::Application::Interaction;
using OpenAnimaTui::Application::KeyResult;
using OpenAnimaTui::Domain::Model;
using OpenAnimaTui::Domain::Role;
using OpenAnimaTui::Presentation::Scope;

constexpr std::size_t Width = 40;
constexpr std::size_t Height = 10;
constexpr std::size_t ScrollStep = 5;

/// A terminal as far as the shell cares: it keeps what was copied and nothing else.
class ClipboardTerminal : public Terminal {
public:
    [[nodiscard]] std::size_t width() const override {
        return Width;
    }
    [[nodiscard]] std::size_t height() const override {
        return Height;
    }
    [[nodiscard]] std::optional<Key> readKey(std::chrono::milliseconds) override {
        return std::nullopt;
    }
    void write(std::string_view) override {}
    [[nodiscard]] bool flush() override {
        return true;
    }
    [[nodiscard]] bool hasPending() const override {
        return false;
    }
    void copyToClipboard(std::string_view text) override {
        copied = std::string{text};
    }

    std::string copied;  ///< What the shell handed to the clipboard.
};

/// A conversation of one row per message, so a row of the screen holds words to take.
Model withRows(int count) {
    Model model;
    for (int index = 0; index < count; ++index) {
        model.messages.push_back({Role::User, "row " + std::to_string(index), false});
    }
    return model;
}

/// A draft taller than the panel that shows it: one line per row given.
Model longDraft(int lines) {
    Model model;
    for (int line = 0; line < lines; ++line) {
        if (line > 0) {
            model.draft += '\n';
        }
        model.draft += "line " + std::to_string(line);
    }
    model.cursor = model.draft.size();
    return model;
}

/// Paints the model and leaves the interaction holding where every part of it sits.
void layOut(Model& model, Interaction& interaction) {
    interaction.shape = OpenAnimaTui::Presentation::render(
        model, Width, Height, interaction.selection, interaction.pointer);
}

Key mouse(Key::Kind kind, std::size_t column, std::size_t row) {
    Key key{kind, 0};
    key.column = column;
    key.row = row;
    return key;
}

KeyResult send(Interaction& interaction, Model& model, const Key& key, std::string& clipboard,
               ClipboardTerminal& terminal) {
    return handleKey(interaction, model, key, ScrollStep, clipboard, terminal);
}

}  // namespace

TEST_CASE("a press starts a span, a drag stretches it and the release lets go") {
    Model model = withRows(3);
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);

    CHECK(send(interaction, model, mouse(Key::Kind::MousePress, 0, 0), clipboard, terminal).used);
    CHECK(interaction.drag == Interaction::Drag::Selection);
    CHECK(interaction.selection.contains(0, 0));

    (void)send(interaction, model, mouse(Key::Kind::MouseDrag, 3, 0), clipboard, terminal);
    CHECK(interaction.selection.contains(0, 3));
    CHECK_FALSE(interaction.selection.contains(0, 4));
    CHECK(interaction.selection.scope() == Scope::Conversation);

    (void)send(interaction, model, mouse(Key::Kind::MouseRelease, 3, 0), clipboard, terminal);
    CHECK(interaction.drag == Interaction::Drag::None);
    CHECK(interaction.selection.contains(0, 3));
}

TEST_CASE("a second press on the same cell takes the word under the pointer") {
    Model model = withRows(3);
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);

    (void)send(interaction, model, mouse(Key::Kind::MousePress, 1, 0), clipboard, terminal);
    (void)send(interaction, model, mouse(Key::Kind::MousePress, 1, 0), clipboard, terminal);
    CHECK(interaction.selection.contains(0, 0));
    CHECK(interaction.selection.contains(0, 2));
    CHECK_FALSE(interaction.selection.contains(0, 3));
}

TEST_CASE("the wheel over the hints walks them and leaves the conversation alone") {
    Model model;
    model.draft = "/";
    model.cursor = model.draft.size();
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);
    REQUIRE(interaction.shape.hintCount > 1);
    CHECK(model.hintIndex == 0);

    const Key wheel{Key::Kind::WheelDown, 0, 0, interaction.shape.hintRow};
    CHECK(send(interaction, model, wheel, clipboard, terminal).used);
    CHECK(model.hintIndex == 1);
}

TEST_CASE("dragging the bar of the conversation moves it") {
    Model model = withRows(30);
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);
    REQUIRE(interaction.shape.maxScrollOffset > 0);
    CHECK(model.scrollOffset == 0);  // The view starts at the bottom.

    // A press beside the thumb brings it under the pointer, which here is the top of the bar.
    (void)send(interaction, model, mouse(Key::Kind::MousePress, Width - 1, 0), clipboard, terminal);
    CHECK(interaction.drag == Interaction::Drag::ConversationBar);
    CHECK(model.scrollOffset == interaction.shape.maxScrollOffset);

    (void)send(interaction, model,
               mouse(Key::Kind::MouseDrag, Width - 1, interaction.shape.scrollRows - 1), clipboard,
               terminal);
    CHECK(model.scrollOffset == 0);

    (void)send(interaction, model, mouse(Key::Kind::MouseRelease, Width - 1, 0), clipboard,
               terminal);
    CHECK(interaction.drag == Interaction::Drag::None);
}

TEST_CASE("dragging the bar of the prompt moves the draft under it") {
    Model model = longDraft(30);
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);
    REQUIRE(interaction.shape.promptTotal > interaction.shape.promptRows);

    const std::size_t column = Width - 2;
    const std::size_t top = interaction.shape.scrollRows + 1;
    (void)send(interaction, model, mouse(Key::Kind::MousePress, column, top), clipboard, terminal);
    REQUIRE(model.promptScroll.has_value());
    CHECK(*model.promptScroll == 0);

    (void)send(interaction, model,
               mouse(Key::Kind::MouseDrag, column, top + interaction.shape.promptRows - 1),
               clipboard, terminal);
    CHECK(*model.promptScroll == interaction.shape.promptTotal - interaction.shape.promptRows);
}

TEST_CASE("the wheel over the prompt moves the draft a row at a time") {
    Model model = longDraft(30);
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);
    REQUIRE(interaction.shape.promptTotal > interaction.shape.promptRows);
    const std::size_t row = interaction.shape.scrollRows + 2;

    const Key up{Key::Kind::WheelUp, 0, 0, row};
    CHECK(send(interaction, model, up, clipboard, terminal).used);
    REQUIRE(model.promptScroll.has_value());
    CHECK(*model.promptScroll == interaction.shape.promptFirst - 1);

    const Key down{Key::Kind::WheelDown, 0, 0, row};
    CHECK(send(interaction, model, down, clipboard, terminal).used);
    CHECK(*model.promptScroll == interaction.shape.promptFirst);
}

TEST_CASE("a drag that waits at an edge of the prompt keeps the draft moving") {
    Model model = longDraft(30);
    model.promptScroll = 6;
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);
    REQUIRE(interaction.shape.promptTotal > interaction.shape.promptRows);

    const std::size_t top = interaction.shape.scrollRows + 1;
    const std::size_t anchor = interaction.shape.scrollRows + 3;
    (void)send(interaction, model, mouse(Key::Kind::MousePress, 10, anchor), clipboard, terminal);
    (void)send(interaction, model, mouse(Key::Kind::MouseDrag, 10, top), clipboard, terminal);
    REQUIRE(interaction.selection.scope() == Scope::Screen);

    const auto start = std::chrono::steady_clock::now();
    CHECK(autoscrollAtEdge(interaction, model, start));
    CHECK(*model.promptScroll == 5);
    // A drag at an edge moves a row at a time, not a row per turn of the loop.
    CHECK_FALSE(autoscrollAtEdge(interaction, model, start + std::chrono::milliseconds{10}));

    // The span stays over the same words, so it moved down with the draft, and the end under the
    // pointer is still on the edge it waits at.
    CHECK(interaction.selection.contains(top, 10));
    CHECK(interaction.selection.contains(anchor + 1, 10));

    // A drag at the bottom edge goes the other way.
    (void)send(interaction, model,
               mouse(Key::Kind::MouseDrag, 10,
                     interaction.shape.scrollRows + interaction.shape.promptRows),
               clipboard, terminal);
    CHECK(autoscrollAtEdge(interaction, model, start + std::chrono::milliseconds{60}));
    CHECK(*model.promptScroll == 6);
}

TEST_CASE("a key that is not the screen's goes to the model, and drops what was picked out") {
    Model model = withRows(3);
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);

    (void)send(interaction, model, mouse(Key::Kind::MousePress, 0, 0), clipboard, terminal);
    REQUIRE(interaction.selection.active());

    const KeyResult result =
        send(interaction, model, Key{Key::Kind::Character, 'a'}, clipboard, terminal);
    CHECK_FALSE(result.used);

    (void)applyKey(interaction, model, Key{Key::Kind::Character, 'a'}, ScrollStep);
    CHECK(model.draft == "a");
    CHECK_FALSE(interaction.selection.active());
}

TEST_CASE("the copy key takes what was picked out") {
    Model model = withRows(3);
    Interaction interaction;
    ClipboardTerminal terminal;
    std::string clipboard;
    layOut(model, interaction);

    (void)send(interaction, model, mouse(Key::Kind::MousePress, 0, 0), clipboard, terminal);
    (void)send(interaction, model, mouse(Key::Kind::MouseDrag, 2, 0), clipboard, terminal);
    (void)send(interaction, model, Key{Key::Kind::CtrlC, 0}, clipboard, terminal);

    CHECK(clipboard == "you");
    CHECK(terminal.copied == "you");
    CHECK_FALSE(interaction.selection.active());
}
