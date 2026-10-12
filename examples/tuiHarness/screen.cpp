#include "screen.h"

#include <algorithm>
#include <utility>

namespace OpenAnimaTui {

namespace {

constexpr std::size_t PrefixWidth = 8;  ///< Width every speaker prefix is padded to.
constexpr std::size_t PromptWidth = 2;  ///< Width of the prompt marker "> ".
constexpr const char* NpcName = "Aldric";

constexpr const char* Dim = "\x1b[2m";
constexpr const char* Bold = "\x1b[1m";
constexpr const char* Reset = "\x1b[0m";
constexpr const char* UserColour = "\x1b[34m";
constexpr const char* NpcColour = "\x1b[32m";

/// The draft as display rows: body text per row, and the position of every character
/// index (including one past the end) as a row and a column within that body.
struct DraftLayout {
    std::vector<std::string> rows;
    std::vector<std::pair<std::size_t, std::size_t>> positions;
};

/// Splits the draft into rows that never exceed the width, at the character level. Input
/// lines wrap at the right edge, the way a line editor does, without rejoining words.
DraftLayout layoutDraft(const std::string& draft, std::size_t width) {
    const std::size_t bodyWidth = width > PromptWidth ? width - PromptWidth : 1;

    DraftLayout layout;
    layout.rows.emplace_back();
    layout.positions.emplace_back(0, 0);
    for (const char character : draft) {
        if (character == '\n') {
            layout.rows.emplace_back();
        } else if (layout.rows.back().size() >= bodyWidth) {
            layout.rows.emplace_back();
            layout.rows.back() += character;
        } else {
            layout.rows.back() += character;
        }
        layout.positions.emplace_back(layout.rows.size() - 1, layout.rows.back().size());
    }
    return layout;
}

/// Pads a speaker name into the prefix it is shown under.
std::string prefix(const std::string& speaker) {
    std::string text = speaker;
    text.resize(std::max(PrefixWidth, speaker.size() + 2), ' ');
    return text;
}

}  // namespace

std::vector<std::string> wrap(const std::string& text, std::size_t width) {
    std::vector<std::string> lines;
    if (width == 0) {
        width = 1;
    }

    std::string line;
    std::string word;
    const auto flushWord = [&] {
        if (word.empty()) {
            return;
        }
        if (line.empty()) {
            line = word;
        } else if (line.size() + 1 + word.size() <= width) {
            line += ' ';
            line += word;
        } else {
            lines.push_back(line);
            line = word;
        }
        word.clear();
    };

    for (const char character : text) {
        if (character == '\n') {
            flushWord();
            lines.push_back(line);
            line.clear();
            continue;
        }
        if (character == ' ') {
            flushWord();
            continue;
        }
        word += character;
        if (word.size() >= width) {
            flushWord();
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
        }
    }
    flushWord();
    if (!line.empty() || lines.empty()) {
        lines.push_back(line);
    }
    return lines;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- the speaker first reads better.
std::vector<std::string> renderLine(const std::string& speaker, const std::string& text,
                                    std::size_t width, Role role) {
    const char* colour = role == Role::Npc ? NpcColour : UserColour;
    const std::size_t bodyWidth = width > PrefixWidth ? width - PrefixWidth : 1;
    const std::vector<std::string> wrapped = wrap(text, bodyWidth);

    std::vector<std::string> lines;
    for (std::size_t index = 0; index < wrapped.size(); ++index) {
        lines.push_back(index == 0 ? std::string{colour} + prefix(speaker) + Reset
                                   : std::string(PrefixWidth, ' '));
        lines.back() += wrapped[index];
    }
    return lines;
}

std::string renderFrame(const ScreenState& state, std::size_t width) {
    std::string frame;
    if (state.busy) {
        const std::vector<std::string> answer =
            renderLine(NpcName, state.answer.empty() ? "..." : state.answer, width, Role::Npc);
        for (const std::string& row : answer) {
            frame += row;
            frame += '\n';
        }
        const std::size_t tenths = elapsedTenths(state);
        frame += std::string(PrefixWidth, ' ') + Dim + "working " + std::to_string(tenths / 10) +
                 "." + std::to_string(tenths % 10) + "s   esc to cancel" + Reset + '\n';
    } else {
        frame +=
            std::string(PrefixWidth, ' ') + Dim + state.model + "   " + state.hint + Reset + '\n';
    }

    const DraftLayout layout = layoutDraft(state.draft, width);
    for (std::size_t row = 0; row < layout.rows.size(); ++row) {
        if (row != 0) {
            frame += '\n';
        }
        frame += row == 0 ? std::string{Bold} + "> " + Reset : std::string(PromptWidth, ' ');
        frame += layout.rows[row];
    }
    return frame;
}

std::pair<std::size_t, std::size_t> caretOffset(const ScreenState& state, std::size_t width) {
    const DraftLayout layout = layoutDraft(state.draft, width);

    const std::size_t index = std::min(state.cursor, state.draft.size());
    const std::pair<std::size_t, std::size_t> caret = layout.positions.at(index);
    const std::pair<std::size_t, std::size_t> end = layout.positions.back();

    const std::size_t upRows = layout.rows.size() - 1 - caret.first;
    const std::size_t leftColumns = (PromptWidth + end.second) - (PromptWidth + caret.second);
    return {upRows, leftColumns};
}

Action mapKey(const Key& key, bool busy, bool draftEmpty) {
    if (busy) {
        switch (key.kind) {
        case Key::Kind::Escape:
        case Key::Kind::CtrlC:
            return Action::Cancel;
        case Key::Kind::CtrlD:
            return Action::Leave;
        default:
            break;
        }
    }

    switch (key.kind) {
    case Key::Kind::Character:
    case Key::Kind::Tab:
        return Action::Insert;
    case Key::Kind::Backspace:
        return Action::Backspace;
    case Key::Kind::Enter:
        return Action::Submit;
    case Key::Kind::CtrlJ:
        return Action::Newline;
    case Key::Kind::Escape:
        return Action::None;
    case Key::Kind::CtrlC:
        return draftEmpty ? Action::Leave : Action::ClearDraft;
    case Key::Kind::CtrlD:
        return Action::Leave;
    case Key::Kind::ArrowUp:
    case Key::Kind::CtrlP:
        return Action::HistoryPrevious;
    case Key::Kind::ArrowDown:
    case Key::Kind::CtrlN:
        return Action::HistoryNext;
    case Key::Kind::ArrowLeft:
    case Key::Kind::CtrlB:
        return Action::MoveLeft;
    case Key::Kind::ArrowRight:
    case Key::Kind::CtrlF:
        return Action::MoveRight;
    case Key::Kind::CtrlA:
        return Action::MoveHome;
    case Key::Kind::CtrlE:
        return Action::MoveEnd;
    case Key::Kind::None:
    case Key::Kind::Unknown:
        return Action::None;
    }
    return Action::None;
}

std::size_t elapsedTenths(const ScreenState& state) {
    if (!state.busy) {
        return 0;
    }
    const auto elapsed = std::chrono::steady_clock::now() - state.startedAt;
    return static_cast<std::size_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 100);
}

}  // namespace OpenAnimaTui
