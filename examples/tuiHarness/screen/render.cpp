#include "render.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "canvas.h"
#include "cell.h"
#include "selection.h"
#include "style.h"
#include "text.h"

namespace OpenAnimaTui::Screen {

namespace {

using App::Message;
using App::Model;
using App::Role;
using Metrics::HintIndent;
using Metrics::MaxHints;
using Metrics::PrefixWidth;
using Metrics::PromptWidth;

/// The draft as display rows: the text of each, where every byte of it lands, and the bytes
/// each row covers. Columns are measured in what the terminal shows, so a wide character takes
/// the two it takes and a combining mark takes none.
struct DraftLayout {
    std::vector<std::string> rows;                               ///< Text per row.
    std::vector<std::pair<std::size_t, std::size_t>> positions;  ///< Byte -> (row, column).
    std::vector<std::pair<std::size_t, std::size_t>> ranges;     ///< Row -> (first, last) byte.
};

/// Splits the draft into rows that never exceed the width.
/// @param draft Draft as typed.
/// @param width Columns available, prompt marker included.
/// @return The rows and where the bytes of the draft land.
DraftLayout layoutDraft(std::string_view draft, std::size_t width) {
    const std::size_t bodyWidth = width > PromptWidth ? width - PromptWidth : 1;

    DraftLayout layout;
    layout.rows.emplace_back();
    layout.positions.emplace_back(0, 0);  // One entry per byte, starting with the first.
    std::size_t rowColumns = 0;
    std::size_t rowStart = 0;

    for (std::size_t offset = 0; offset < draft.size();) {
        const std::size_t start = offset;
        const std::uint32_t codepoint = decodeCodepoint(draft, offset);
        const std::string_view bytes = draft.substr(start, offset - start);
        const std::size_t columns = codepoint == '\n' ? 1 : glyphColumns(codepoint);

        if (codepoint == '\n') {
            layout.ranges.emplace_back(rowStart, start);
            layout.rows.emplace_back();
            rowColumns = 0;
            rowStart = offset;
        } else if (rowColumns + columns > bodyWidth) {
            layout.ranges.emplace_back(rowStart, start);
            layout.rows.emplace_back();
            rowColumns = 0;
            rowStart = start;
        }
        if (codepoint != '\n') {
            layout.rows.back() += bytes;
            rowColumns += columns;
        }
        // The caret is moved byte by byte, so every byte of a character points at the column the
        // whole character ends on: the caret never sits inside one.
        for (std::size_t index = start; index < offset; ++index) {
            layout.positions.emplace_back(layout.rows.size() - 1, rowColumns);
        }
    }
    layout.ranges.emplace_back(rowStart, draft.size());
    return layout;
}

/// How long the command token is: the leading word when the draft is a command.
/// @param draft Draft as typed.
/// @return Bytes of the token, zero when the draft is not a command.
std::size_t commandTokenLength(std::string_view draft) {
    if (draft.empty() || draft[0] != '/') {
        return 0;
    }
    const std::size_t space = draft.find(' ');
    return space == std::string_view::npos ? draft.size() : space;
}

/// Pads a speaker name into the prefix it is shown under.
/// @param speaker Name to show.
/// @return The prefix, at least `PrefixWidth` columns wide.
std::string prefix(const std::string& speaker) {
    std::string text = speaker;
    text.resize(std::max(PrefixWidth, speaker.size() + 2), ' ');
    return text;
}

/// One row of the conversation, before it is painted.
struct TranscriptRow {
    std::string prefix;  ///< Who is speaking, padded to `PrefixWidth` columns.
    std::string body;    ///< What they say.
    Style prefixStyle;   ///< How the name is painted.
    Style bodyStyle;     ///< How the text is painted.
};

/// Adds the rows of one message, wrapped to the body width.
/// @param rows Rows collected so far, appended to.
/// @param message Message to lay out.
/// @param speaker Name shown at the start of the first row.
/// @param nameStyle Style of that name.
/// @param bodyStyle Style of the text.
/// @param width Columns available for the body.
void appendMessage(std::vector<TranscriptRow>& rows, const Message& message,
                   const std::string& speaker, const Style& nameStyle, const Style& bodyStyle,
                   std::size_t width) {
    const std::vector<std::string> wrapped = wrap(message.text, width);
    for (std::size_t index = 0; index < wrapped.size(); ++index) {
        rows.push_back({index == 0 ? prefix(speaker) : std::string(PrefixWidth, ' '),
                        wrapped[index], nameStyle, bodyStyle});
    }
}

/// Adds a note, which belongs to no speaker and is shown quietly.
/// @param rows Rows collected so far, appended to.
/// @param text Note to lay out.
/// @param width Columns available for the body.
void appendNote(std::vector<TranscriptRow>& rows, const std::string& text, std::size_t width) {
    for (const std::string& line : wrap(text, width)) {
        rows.push_back({std::string(PrefixWidth, ' '), line, Palette::muted(), Palette::muted()});
    }
}

/// Adds the blank row that keeps a message from running into the one before it.
/// @param rows Rows collected so far, appended to.
void appendGap(std::vector<TranscriptRow>& rows) {
    rows.push_back({std::string(PrefixWidth, ' '), {}, Palette::text(), Palette::text()});
}

/// The conversation as display rows: finished turns first, the in-flight answer last.
/// @param model Model to show.
/// @param width Columns available.
/// @return One entry per row.
std::vector<TranscriptRow> transcriptRows(const Model& model, std::size_t width) {
    const std::size_t bodyWidth = width > PrefixWidth ? width - PrefixWidth : 1;
    const std::string npc =
        model.world.active.has_value() ? "npc " + std::to_string(*model.world.active) : "npc";

    std::vector<TranscriptRow> rows;
    for (const Message& message : model.messages) {
        // Every message is separated from the one before it, so a turn reads as a question and
        // the answer it got rather than as one block of text.
        if (!rows.empty()) {
            appendGap(rows);
        }
        if (message.role == Role::System) {
            appendNote(rows, message.text, bodyWidth);
            continue;
        }
        const bool fromUser = message.role == Role::User;
        appendMessage(rows, message, fromUser ? "you" : npc,
                      fromUser ? Palette::muted() : Palette::primary(), Palette::text(), bodyWidth);
    }
    if (model.busy || !model.answer.empty()) {
        if (!rows.empty()) {
            appendGap(rows);
        }
        const Message answer{Role::Npc, model.answer.empty() ? "..." : model.answer};
        appendMessage(rows, answer, npc, Palette::primary(), Palette::text(), bodyWidth);
    }
    return rows;
}

/// The one status row: what is working while a turn runs, or the model and what can be done.
/// @param model Model to show.
/// @return The row, painted later.
std::string statusRow(const Model& model) {
    if (model.busy) {
        const std::size_t tenths = App::elapsedTenths(model);
        const std::size_t frame = tenths % Glyphs::Spinner.size();
        const std::string tail = model.cancelling ? "   cancelling" : "   esc to cancel";
        return std::string(PrefixWidth - 1, ' ') + Glyphs::Spinner.at(frame) + " " + "working " +
               std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + "s" + tail;
    }
    const char* hint = model.world.active.has_value()
                           ? "ask something   /help for keys"
                           : "no npc   /create to make one   /help for keys";
    const std::string mode = model.raw ? "   raw" : "";
    return std::string(PrefixWidth, ' ') + model.modelName + "   " + hint + mode;
}

/// Paint what the mouse picked out, over whatever the frame holds there: a selection is about the
/// screen, not about the conversation behind it. A drag that is down to one cell still shows,
/// which is why this asks whether the mouse is holding something and not whether there is
/// something worth copying.
/// @param frame Frame to paint on.
/// @param selection Cells the mouse picked out.
void pickOutSelection(Canvas& frame, const Selection& selection) {
    if (!selection.active()) {
        return;
    }
    const std::size_t last = std::min(selection.lastRow(), frame.height() - 1);
    for (std::size_t picked = selection.firstRow(); picked <= last; ++picked) {
        for (std::size_t column = 0; column < frame.width(); ++column) {
            if (!selection.contains(picked, column)) {
                continue;
            }
            Cell& cell = frame.at(picked, column);
            cell.style = cell.style.withReverse();
        }
    }
}

}  // namespace

RenderResult render(const Model& model, std::size_t width, std::size_t height,
                    const Selection& selection) {
    RenderResult result{Canvas{width, height}, 0, 0};
    Canvas& frame = result.frame;
    if (width == 0 || height == 0) {
        return result;
    }

    const std::vector<TranscriptRow> transcript = transcriptRows(model, width);
    const DraftLayout prompt = layoutDraft(model.draft, width);

    const std::vector<std::string> all = App::hintsFor(model);
    const std::size_t selected = all.empty() ? 0 : std::min(model.hintIndex, all.size() - 1);

    // The panel shows a window of the list, and the window follows the selection, so a
    // command reached with the arrows is never off the panel.
    std::size_t first = 0;
    if (all.size() > MaxHints) {
        first = selected >= MaxHints ? selected - MaxHints + 1 : 0;
        first = std::min(first, all.size() - MaxHints);
    }
    const std::size_t last = std::min(all.size(), first + MaxHints);
    const std::vector<std::string> hints(all.begin() + static_cast<std::ptrdiff_t>(first),
                                         all.begin() + static_cast<std::ptrdiff_t>(last));

    const bool withStatus = height > prompt.rows.size() + hints.size();
    const std::size_t bottomRows = prompt.rows.size() + hints.size() + (withStatus ? 1 : 0);
    const std::size_t viewRows = height > bottomRows ? height - bottomRows : 0;

    // The viewport of the conversation, scrolled by the model.
    const std::size_t total = transcript.size();
    std::size_t start = 0;
    if (total > viewRows) {
        const std::size_t offset = std::min(model.scrollOffset, total - viewRows);
        start = total - viewRows - offset;
    }
    for (std::size_t row = 0; row < viewRows; ++row) {
        if (row < total) {
            const TranscriptRow& line = transcript[start + row];
            frame.put(row, 0, line.prefix, line.prefixStyle);
            frame.put(row, PrefixWidth, line.body, line.bodyStyle);
        }
    }

    // The hints float over the conversation, on their own panel.
    std::size_t row = viewRows;
    const std::size_t highlighted = selected - first;
    for (std::size_t index = 0; index < hints.size(); ++index) {
        const Style style = (index == highlighted ? Palette::accent() : Palette::muted())
                                .withBackground(Palette::raisedColor());
        frame.fill(row, 0, width, Palette::raised());
        frame.put(row, HintIndent, hints[index], style);
        ++row;
    }

    if (withStatus) {
        frame.fill(row, 0, width, Palette::panel());
        frame.put(row, 0, statusRow(model), Palette::muted().withBackground(Palette::panelColor()));
        ++row;
    }

    // The prompt, which is the part that stays put while the conversation moves.
    const std::size_t commandLength = commandTokenLength(model.draft);
    for (std::size_t index = 0; index < prompt.rows.size(); ++index) {
        frame.fill(row, 0, width, Palette::panel());
        const std::size_t column = PromptWidth;
        if (index == 0) {
            frame.put(row, 0, Glyphs::PromptMarker,
                      Palette::accent().withBackground(Palette::panelColor()));
        }

        const std::size_t rangeStart = prompt.ranges[index].first;
        const std::string& text = prompt.rows[index];
        const std::size_t token =
            commandLength > rangeStart ? std::min(commandLength - rangeStart, text.size()) : 0;
        if (token > 0) {
            frame.put(row, column, std::string_view{text}.substr(0, token),
                      Palette::accent().withBackground(Palette::panelColor()));
        }
        frame.put(row, column + token, std::string_view{text}.substr(token),
                  Palette::text().withBackground(Palette::panelColor()));
        ++row;
    }

    const auto position = prompt.positions.at(std::min(model.cursor, model.draft.size()));
    result.caretRow = viewRows + hints.size() + (withStatus ? 1 : 0) + position.first;
    result.caretColumn = PromptWidth + position.second;

    pickOutSelection(frame, selection);
    return result;
}

}  // namespace OpenAnimaTui::Screen
