#include "render.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "app/commands.h"
#include "style.h"
#include "text.h"

namespace OpenAnimaTui::Screen {

namespace {

using App::Message;
using App::Model;
using App::Role;

/// The draft as display rows: the body of each, the position of every character index, and
/// the character range each row covers. Columns are code points, so accented input does not
/// push the caret or the wrapping out of step.
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
    layout.positions.emplace_back(0, 0);  // One entry per byte, starting with the first.
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
            rowColumns = 0;
            rowStart = index;
            continue;
        } else {
            layout.rows.back() += draft.substr(index, length);
            ++rowColumns;
            index += length;
        }
        // The caret is moved byte by byte, so every byte of a character points at the column
        // the whole character ends on: the caret never sits inside one.
        for (std::size_t byte = 0; byte < length; ++byte) {
            layout.positions.emplace_back(layout.rows.size() - 1, rowColumns);
        }
    }
    layout.ranges.emplace_back(rowStart, draft.size());
    return layout;
}

/// How long the command token is: the leading word when the draft is a command.
std::size_t commandTokenLength(const std::string& draft) {
    if (draft.empty() || draft[0] != '/') {
        return 0;
    }
    const std::size_t space = draft.find(' ');
    return space == std::string::npos ? draft.size() : space;
}

/// Pads a row and paints it over a panel background.
std::string paint(const std::string& content, std::size_t width, const char* background) {
    return std::string{background} + padRow(content, width) + Reset;
}

/// Pads a speaker name into the prefix it is shown under.
std::string prefix(const std::string& speaker) {
    std::string text = speaker;
    text.resize(std::max(PrefixWidth, speaker.size() + 2), ' ');
    return text;
}

/// Who is shown as speaking a line: the name of the prefix and the colour it is painted with.
struct Speaker {
    std::string name;  ///< Shown at the start of the first row.
    const char* tint;  ///< SGR sequence for the prefix.
};

/// Renders one committed line: the speaker prefix and the wrapped body.
std::vector<std::string> renderLine(const Speaker& speaker, const std::string& text,
                                    std::size_t width) {
    const std::size_t bodyWidth = width > PrefixWidth ? width - PrefixWidth : 1;
    const std::vector<std::string> wrapped = wrap(text, bodyWidth);

    std::vector<std::string> lines;
    for (std::size_t index = 0; index < wrapped.size(); ++index) {
        lines.push_back(index == 0 ? std::string{speaker.tint} + prefix(speaker.name) + Reset
                                   : std::string(PrefixWidth, ' '));
        lines.back() += wrapped[index];
    }
    return lines;
}

/// How the NPC being talked to is shown, or a stand-in when there is none.
std::string npcLabel(const Model& model) {
    return model.world.active.has_value() ? "npc " + std::to_string(*model.world.active) : "npc";
}

/// The name and colour of the one speaking a message.
Speaker speakerOf(const Model& model, Role role) {
    return role == Role::User ? Speaker{"you", Muted} : Speaker{npcLabel(model), Primary};
}

/// The conversation as display rows: finished turns first, the in-flight answer last.
std::vector<std::string> transcriptLines(const Model& model, std::size_t width) {
    std::vector<std::string> lines;
    for (const Message& message : model.messages) {
        if (message.role == Role::System) {
            for (const std::string& line :
                 wrap(message.text, width > PrefixWidth ? width - PrefixWidth : 1)) {
                lines.push_back(Muted + std::string(PrefixWidth, ' ') + line + Reset);
            }
            continue;
        }
        for (std::string& line : renderLine(speakerOf(model, message.role), message.text, width)) {
            lines.push_back(std::move(line));
        }
    }
    if (model.busy || !model.answer.empty()) {
        const Speaker speaker{npcLabel(model), Primary};
        for (std::string& line :
             renderLine(speaker, model.answer.empty() ? "..." : model.answer, width)) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

/// The one status row: what is working while a turn runs, or the model and what can be done.
std::string statusLine(const Model& model) {
    if (model.busy) {
        const std::size_t tenths = App::elapsedTenths(model);
        const std::size_t spinner = tenths % SpinnerFrames.size();
        const std::string tail = model.cancelling ? "   cancelling" : "   esc to cancel";
        return std::string(PrefixWidth - 1, ' ') + Accent + SpinnerFrames.at(spinner) +
               ForegroundReset + ' ' + Muted + "working " + std::to_string(tenths / 10) + "." +
               std::to_string(tenths % 10) + "s" + tail;
    }
    const char* hint = model.world.active.has_value()
                           ? "ask something   /help for keys"
                           : "no npc   /create to make one   /help for keys";
    return std::string(PrefixWidth, ' ') + Muted + model.modelName + "   " + hint;
}

/// The prompt as display rows, with the command token in its own colour.
std::vector<std::string> promptRows(const std::string& draft, const DraftLayout& layout) {
    const std::size_t commandLength = commandTokenLength(draft);
    std::vector<std::string> rows;
    for (std::size_t row = 0; row < layout.rows.size(); ++row) {
        const std::size_t start = layout.ranges[row].first;
        std::string body;
        const std::size_t coloured =
            commandLength > start ? std::min(commandLength - start, layout.rows[row].size()) : 0;
        if (coloured > 0) {
            body = std::string{Accent} + layout.rows[row].substr(0, coloured) + ForegroundReset +
                   layout.rows[row].substr(coloured);
        } else {
            body = layout.rows[row];
        }
        rows.push_back(row == 0 ? std::string{Accent} + PromptMarker + " " + ForegroundReset + body
                                : std::string(PromptWidth, ' ') + body);
    }
    return rows;
}

}  // namespace

RenderResult render(const Model& model, std::size_t width, std::size_t height) {
    const std::vector<std::string> transcript = transcriptLines(model, width);

    const DraftLayout promptLayout = layoutDraft(model.draft, width);
    const std::vector<std::string> prompt = promptRows(model.draft, promptLayout);

    std::vector<std::string> hints = App::hintsFor(model);
    if (hints.size() > MaxHints) {
        hints.resize(MaxHints);
    }

    const bool showStatus = height > prompt.size() + hints.size();
    const std::size_t bottomRows = prompt.size() + hints.size() + (showStatus ? 1 : 0);
    const std::size_t viewRows = height > bottomRows ? height - bottomRows : 0;

    // The viewport of the conversation, scrolled by the model.
    std::vector<std::string> screen;
    screen.reserve(height);
    const std::size_t total = transcript.size();
    std::size_t start = 0;
    if (total > viewRows) {
        const std::size_t offset = std::min(model.scrollOffset, total - viewRows);
        start = total - viewRows - offset;
    }
    for (std::size_t row = 0; row < viewRows; ++row) {
        screen.push_back(row < total ? padRow(transcript[start + row], width)
                                     : std::string(width, ' '));
    }

    // The hints float over the conversation, on their own raised panel.
    const std::size_t selected = hints.empty() ? 0 : std::min(model.hintIndex, hints.size() - 1);
    for (std::size_t index = 0; index < hints.size(); ++index) {
        const std::string content = std::string("  ") + (index == selected ? Accent : Muted) +
                                    hints[index] + ForegroundReset;
        screen.push_back(paint(content, width, Raised));
    }

    if (showStatus) {
        screen.push_back(paint(statusLine(model), width, Panel));
    }

    for (const std::string& row : prompt) {
        screen.push_back(paint(row, width, Panel));
    }

    RenderResult result;
    result.text.reserve(height * (width + 1));
    for (std::size_t row = 0; row < screen.size(); ++row) {
        if (row != 0) {
            result.text += '\n';
        }
        result.text += screen[row];
    }
    const auto [caretPromptRow, caretBodyCol] =
        promptLayout.positions.at(std::min(model.cursor, model.draft.size()));
    result.caretRow = viewRows + hints.size() + (showStatus ? 1 : 0) + caretPromptRow;
    result.caretCol = PromptWidth + caretBodyCol;
    return result;
}

}  // namespace OpenAnimaTui::Screen
