#include "screen.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

namespace OpenAnimaTui {

namespace {

constexpr std::size_t PrefixWidth = 8;  ///< Width every speaker prefix is padded to.
constexpr std::size_t PromptWidth = 2;  ///< Width of the prompt marker, one glyph plus a space.
constexpr std::size_t MaxHints = 6;     ///< Commands shown while typing one.

// The OpenCode dark palette, taken from its theme.json, as 24-bit SGR.
constexpr const char* Accent = "\x1b[38;2;157;124;216m";   // #9d7cd8
constexpr const char* Primary = "\x1b[38;2;250;178;131m";  // #fab283
constexpr const char* Muted = "\x1b[38;2;128;128;128m";    // #808080
constexpr const char* Reset = "\x1b[0m";

/// The prompt marker, as OpenCode draws its prompts, and the dots it spins while working.
constexpr const char* PromptMarker = "\u2771";
constexpr std::array<const char*, 10> SpinnerFrames = {
    "\u280B", "\u2819", "\u2839", "\u2838", "\u283C",
    "\u2834", "\u2826", "\u2827", "\u2807", "\u280F",
};

/// How many bytes a UTF-8 code point takes, from its leading byte.
std::size_t utf8Length(unsigned char byte) {
    if (byte < 0x80) {
        return 1;
    }
    if ((byte >> 5) == 0x6) {
        return 2;
    }
    if ((byte >> 4) == 0xE) {
        return 3;
    }
    if ((byte >> 3) == 0x1E) {
        return 4;
    }
    return 1;
}

/// Columns a row occupies, ignoring the colour sequences it carries.
std::size_t visibleColumns(const std::string& text) {
    std::size_t columns = 0;
    for (std::size_t index = 0; index < text.size();) {
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
        index += utf8Length(static_cast<unsigned char>(text[index]));
        ++columns;
    }
    return columns;
}

/// Pads a row to the width with trailing spaces, so a full redraw leaves no leftovers.
std::string padRow(const std::string& row, std::size_t width) {
    const std::size_t columns = visibleColumns(row);
    return columns >= width ? row : row + std::string(width - columns, ' ');
}

/// The draft as display rows: body text per row, the position of every character index, and
/// the character range each row covers. Columns are code points, so wide or accented input
/// does not push the caret or the wrapping out of step.
struct DraftLayout {
    std::vector<std::string> rows;                               ///< Body text per row.
    std::vector<std::pair<std::size_t, std::size_t>> positions;  ///< Char -> (row, column).
    std::vector<std::pair<std::size_t, std::size_t>> ranges;     ///< Row -> (first, last) char.
};

/// Splits the draft into rows that never exceed the width, at the character level.
DraftLayout layoutDraft(const std::string& draft, std::size_t width) {
    const std::size_t bodyWidth = width > PromptWidth ? width - PromptWidth : 1;

    DraftLayout layout;
    layout.rows.emplace_back();
    layout.positions.emplace_back(0, 0);
    std::size_t rowColumns = 0;
    std::size_t rowStart = 0;

    for (std::size_t index = 0; index < draft.size();) {
        const std::size_t length = utf8Length(static_cast<unsigned char>(draft[index]));
        if (draft[index] == '\n') {
            layout.ranges.emplace_back(rowStart, index);
            layout.rows.emplace_back();
            rowColumns = 0;
            rowStart = index + 1;
            index += 1;
        } else if (rowColumns >= bodyWidth) {
            layout.ranges.emplace_back(rowStart, index);
            layout.rows.emplace_back();
            layout.rows.back() += draft.substr(index, length);
            rowColumns = 1;
            rowStart = index;
            index += length;
        } else {
            layout.rows.back() += draft.substr(index, length);
            ++rowColumns;
            index += length;
        }
        layout.positions.emplace_back(layout.rows.size() - 1, rowColumns);
    }
    layout.ranges.emplace_back(rowStart, draft.size());
    return layout;
}

/// Pads a speaker name into the prefix it is shown under.
std::string prefix(const std::string& speaker) {
    std::string text = speaker;
    text.resize(std::max(PrefixWidth, speaker.size() + 2), ' ');
    return text;
}

/// How long the command token is: the leading word when the draft is a command.
std::size_t commandTokenLength(const std::string& draft) {
    if (draft.empty() || draft[0] != '/') {
        return 0;
    }
    const std::size_t space = draft.find(' ');
    return space == std::string::npos ? draft.size() : space;
}

/// The conversation as display rows: finished turns first, the in-flight answer last.
std::vector<std::string> transcriptLines(const ScreenState& state, std::size_t width) {
    const std::string npc = state.npcLabel.empty() ? "npc" : state.npcLabel;
    std::vector<std::string> lines;
    for (const Message& message : state.messages) {
        if (message.role == Role::System) {
            for (const std::string& line :
                 wrap(message.text, width > PrefixWidth ? width - PrefixWidth : 1)) {
                lines.push_back(Muted + std::string(PrefixWidth, ' ') + line + Reset);
            }
            continue;
        }
        const std::string speaker = message.role == Role::User ? "you" : npc;
        for (std::string& line : renderLine(speaker, message.text, width, message.role)) {
            lines.push_back(std::move(line));
        }
    }
    if (state.busy || !state.answer.empty()) {
        for (std::string& line :
             renderLine(npc, state.answer.empty() ? "..." : state.answer, width, Role::Npc)) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

/// The one status row: what is working while a turn runs, or the model and the hint.
std::string statusLine(const ScreenState& state) {
    if (!state.busy) {
        return std::string(PrefixWidth, ' ') + Muted + state.model + "   " + state.hint + Reset;
    }
    const std::size_t tenths = elapsedTenths(state);
    const std::size_t spinner = tenths % SpinnerFrames.size();
    return std::string(PrefixWidth - 1, ' ') + Accent + SpinnerFrames.at(spinner) + Reset + ' ' +
           Muted + "working " + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) +
           "s   esc to cancel" + Reset;
}

/// The prompt as display rows, with the command token coloured.
std::vector<std::string> promptRows(const DraftLayout& layout, std::size_t commandLength) {
    std::vector<std::string> rows;
    for (std::size_t row = 0; row < layout.rows.size(); ++row) {
        const std::size_t start = layout.ranges[row].first;
        std::string body;
        const std::size_t coloured =
            commandLength > start ? std::min(commandLength - start, layout.rows[row].size()) : 0;
        if (coloured > 0) {
            body = std::string{Accent} + layout.rows[row].substr(0, coloured) + Reset +
                   layout.rows[row].substr(coloured);
        } else {
            body = layout.rows[row];
        }
        rows.push_back(row == 0 ? std::string{Accent} + PromptMarker + " " + Reset + body
                                : std::string(PromptWidth, ' ') + body);
    }
    return rows;
}

}  // namespace

std::vector<std::string> wrap(const std::string& text, std::size_t width) {
    std::vector<std::string> lines;
    if (width == 0) {
        width = 1;
    }

    std::string line;
    std::size_t lineColumns = 0;
    std::string word;
    std::size_t wordColumns = 0;

    const auto flushWord = [&] {
        if (word.empty()) {
            return;
        }
        if (line.empty()) {
            line = word;
            lineColumns = wordColumns;
        } else if (lineColumns + 1 + wordColumns <= width) {
            line += ' ';
            line += word;
            lineColumns += 1 + wordColumns;
        } else {
            lines.push_back(line);
            line = word;
            lineColumns = wordColumns;
        }
        word.clear();
        wordColumns = 0;
    };

    for (std::size_t index = 0; index < text.size();) {
        const std::size_t length = utf8Length(static_cast<unsigned char>(text[index]));
        const std::string codepoint = text.substr(index, length);
        if (codepoint == "\n") {
            flushWord();
            lines.push_back(line);
            line.clear();
            lineColumns = 0;
        } else if (codepoint == " ") {
            flushWord();
        } else {
            word += codepoint;
            ++wordColumns;
            if (wordColumns >= width) {
                flushWord();
                if (!line.empty()) {
                    lines.push_back(line);
                    line.clear();
                    lineColumns = 0;
                }
            }
        }
        index += length;
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
    const char* colour = role == Role::Npc ? Primary : Muted;
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

const std::vector<std::string>& commands() {
    static const std::vector<std::string> list = {
        "/create", "/enter", "/npcs", "/leave", "/help", "/history clear", "/quit",
    };
    return list;
}

std::vector<std::string> commandMatches(const std::string& draft, std::size_t cursor) {
    std::vector<std::string> matches;
    const std::string typed = draft.substr(0, std::min(cursor, draft.size()));
    if (typed.empty() || typed[0] != '/' || typed.find(' ') != std::string::npos) {
        return matches;
    }
    for (const std::string& command : commands()) {
        if (command.rfind(typed, 0) == 0) {
            matches.push_back(command);
        }
    }
    return matches;
}

std::pair<std::string, std::size_t> completeCommand(const std::string& draft, std::size_t cursor) {
    const std::vector<std::string> matches = commandMatches(draft, cursor);
    if (matches.empty()) {
        return {draft, cursor};
    }

    const std::string typed = draft.substr(0, std::min(cursor, draft.size()));
    const std::string rest = draft.substr(std::min(cursor, draft.size()));

    if (matches.size() == 1) {
        const std::string completed = matches.front() + " ";
        return {completed + rest, completed.size()};
    }

    std::string common = matches.front();
    for (const std::string& match : matches) {
        while (match.rfind(common, 0) != 0) {
            common.pop_back();
        }
    }
    if (common == typed) {
        return {draft, cursor};
    }
    return {common + rest, common.size()};
}

RenderResult renderScreen(const ScreenState& state, std::size_t width, std::size_t height) {
    const std::vector<std::string> transcript = transcriptLines(state, width);

    const DraftLayout promptLayout = layoutDraft(state.draft, width);
    const std::vector<std::string> prompt =
        promptRows(promptLayout, commandTokenLength(state.draft));

    const std::size_t caretIndex = std::min(state.cursor, state.draft.size());
    const auto [caretPromptRow, caretBodyCol] = promptLayout.positions.at(caretIndex);

    // The commands that complete what is being typed, shown floating above the prompt.
    std::vector<std::string> hints = commandMatches(state.draft, state.cursor);
    if (hints.size() > MaxHints) {
        hints.resize(MaxHints);
    }
    const bool showStatus = height > prompt.size() + hints.size();
    const std::size_t bottomRows = prompt.size() + hints.size() + (showStatus ? 1 : 0);
    const std::size_t viewRows = height > bottomRows ? height - bottomRows : 0;

    // The viewport of the conversation, scrolled by the state.
    std::vector<std::string> screen;
    screen.reserve(height);
    const std::size_t total = transcript.size();
    std::size_t start = 0;
    if (total > viewRows) {
        const std::size_t offset = std::min(state.scrollOffset, total - viewRows);
        start = total - viewRows - offset;
    }
    for (std::size_t row = 0; row < viewRows; ++row) {
        screen.push_back(row < total ? padRow(transcript[start + row], width)
                                     : std::string(width, ' '));
    }

    for (std::size_t index = 0; index < hints.size(); ++index) {
        const std::string colour = index == 0 ? Accent : Muted;
        screen.push_back(padRow(std::string(colour) + "  " + hints[index] + Reset, width));
    }

    if (showStatus) {
        screen.push_back(padRow(statusLine(state), width));
    }

    for (const std::string& row : prompt) {
        screen.push_back(padRow(row, width));
    }

    RenderResult result;
    result.text.reserve(height * (width + 1));
    for (std::size_t row = 0; row < screen.size(); ++row) {
        if (row != 0) {
            result.text += '\n';
        }
        result.text += screen[row];
    }
    result.caretRow = viewRows + hints.size() + (showStatus ? 1 : 0) + caretPromptRow;
    result.caretCol = PromptWidth + caretBodyCol;
    return result;
}

Action mapKey(const Key& key, bool busy, bool draftEmpty, bool command) {
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
        return Action::Insert;
    case Key::Kind::Tab:
        return command ? Action::Complete : Action::Insert;
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
    case Key::Kind::PageUp:
        return Action::ScrollUp;
    case Key::Kind::PageDown:
        return Action::ScrollDown;
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
