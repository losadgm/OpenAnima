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
using Metrics::BoxWidth;
using Metrics::HintIndent;
using Metrics::MaxHints;
using Metrics::PanelPad;
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
/// @param width Columns the text may use.
/// @return The rows and where the bytes of the draft land.
DraftLayout layoutDraft(std::string_view draft, std::size_t width) {
    const std::size_t bodyWidth = width > 0 ? width : 1;

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
        // Turns are separated from each other, but the lines of one reply are not: they are a
        // block, and the name on its first line is the name of all of them.
        if (!rows.empty() && !message.continues) {
            appendGap(rows);
        }
        if (message.role == Role::System) {
            appendNote(rows, message.text, bodyWidth);
            continue;
        }
        const bool fromUser = message.role == Role::User;
        std::string speaker;
        if (!message.continues) {
            speaker = fromUser ? "you" : npc;
        }
        const Style nameStyle = fromUser ? Palette::muted() : Palette::primary();
        const Style bodyStyle = message.role == Role::Thought ? Palette::muted() : Palette::text();
        appendMessage(rows, message, speaker, nameStyle, bodyStyle, bodyWidth);
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
/// It starts at the left edge, where the frame of the prompt starts, and sits on the plain
/// background rather than on a panel of its own.
/// @param model Model to show.
/// @return The row, painted later.
std::string statusRow(const Model& model) {
    if (model.busy) {
        const std::size_t tenths = App::elapsedTenths(model);
        const std::size_t frame = tenths % Glyphs::Spinner.size();
        const std::string tail = model.cancelling ? "   cancelling" : "   esc to cancel";
        return Glyphs::Spinner.at(frame) + std::string{" working "} + std::to_string(tenths / 10) +
               "." + std::to_string(tenths % 10) + "s" + tail;
    }
    const char* hint = model.world.active.has_value()
                           ? "ask something   /help for keys"
                           : "no npc   /create to make one   /help for keys";
    const std::string mode = model.raw ? "   raw" : "";
    if (model.modelName.empty()) {
        return std::string{hint} + mode;
    }
    return model.modelName + "   " + hint + mode;
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

/// The hints the panel shows: a window of the list around the selection, so a command reached
/// with the arrows is never off the panel.
/// @param all Every hint that matches what is typed.
/// @param selected Which one is selected.
/// @param first Receives the index of the first hint of the window, in the whole list.
/// @return The hints of the window, in order.
std::vector<std::string> hintWindow(const std::vector<std::string>& all, std::size_t selected,
                                    std::size_t& first) {
    first = 0;
    if (all.size() <= MaxHints) {
        return all;
    }
    if (selected >= MaxHints) {
        first = selected - MaxHints + 1;
    }
    first = std::min(first, all.size() - MaxHints);
    return std::vector<std::string>{all.begin() + static_cast<std::ptrdiff_t>(first),
                                    all.begin() + static_cast<std::ptrdiff_t>(first + MaxHints)};
}

/// Paints the scrollbar: one column on the right, a picture of where the viewport sits and
/// nothing else. The track is dim, the thumb is not.
/// @param frame Canvas to paint on.
/// @param viewRows Rows the conversation occupies.
/// @param width Columns available.
/// @param total Rows the whole conversation takes.
/// @param start First row of the conversation that is on screen.
// The rows, the columns and the two counts are named at the only call site there is.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void paintScrollBar(Canvas& frame, std::size_t viewRows, std::size_t width, std::size_t total,
                    std::size_t start) {
    if (total <= viewRows || viewRows == 0 || width < 2) {
        return;
    }

    const std::size_t column = width - 1;
    for (std::size_t row = 0; row < viewRows; ++row) {
        frame.put(row, column, Glyphs::ScrollBar, Palette::muted().withDim());
    }

    const std::size_t thumb = std::max<std::size_t>(1, viewRows * viewRows / total);
    const std::size_t travel = viewRows - thumb;
    const std::size_t range = total - viewRows;
    const std::size_t firstRow = start * travel / range;
    for (std::size_t row = 0; row < thumb; ++row) {
        frame.put(firstRow + row, column, Glyphs::ScrollBar, Palette::muted());
    }
}

/// Paints one horizontal border of the frame around the prompt.
///
/// The border sits on the plain background, so the change of colour is exactly where the frame
/// is: everything inside it is the panel, everything outside is not. The stroke spans the width
/// of the panel and not a cell more, so it ends where the side strokes are drawn and the two meet
/// at the corners of the panel. No corner glyph is drawn, because a corner glyph would be a cell
/// of its own, outside the panel it is supposed to be the corner of.
/// @param frame Canvas to paint on.
/// @param row Row to paint.
/// @param width Columns available.
/// @param top Whether it is the upper border.
// A row and a width are both counts, and both are named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void paintBoxBorder(Canvas& frame, std::size_t row, std::size_t width, bool top) {
    const Style border = Palette::muted();
    for (std::size_t column = 0; column + BoxWidth < width; ++column) {
        frame.put(row, column, top ? Glyphs::BoxTop : Glyphs::BoxBottom, border);
    }
}

/// What the empty prompt says: what can be done from here, which is not the same in the lobby
/// as in a conversation.
/// @param model Model to read.
/// @return The text, which the prompt draws in the quiet colour.
std::string placeholderOf(const Model& model) {
    return model.world.active.has_value() ? std::string{"say something, or /help"}
                                          : std::string{"/create <description> to make an npc"};
}

/// Paints the prompt inside the frame that holds it, the frame's top border included.
/// @param frame Canvas to paint on.
/// @param model Model to read the draft and the placeholder from.
/// @param prompt The draft as it was laid out.
/// @param firstRow Row the top border goes on.
/// @param width Columns available.
void paintPrompt(Canvas& frame, const Model& model, const DraftLayout& prompt, std::size_t firstRow,
                 std::size_t width) {
    const std::size_t markerColumn = PanelPad;
    const std::size_t textColumn = markerColumn + PromptWidth;
    const std::size_t commandLength = commandTokenLength(model.draft);
    const Style border = Palette::muted();
    const Style onPanel = border.withBackground(Palette::panelColor());

    // The panel is the box, and the strokes are its edges: the left one is drawn on the panel
    // itself, in the column it takes, and the right one in the column after it. It keeps a row
    // empty above the line being typed and another one below, so the text sits in the middle of
    // a band of colour instead of against the stroke.
    paintBoxBorder(frame, firstRow, width, true);
    const std::size_t panelRows = prompt.rows.size() + 2;
    for (std::size_t index = 0; index < panelRows; ++index) {
        const std::size_t row = firstRow + 1 + index;
        if (width > BoxWidth) {
            frame.fill(row, 0, width - BoxWidth, Palette::panel());
        }
        frame.put(row, 0, Glyphs::BoxSide, onPanel);
        frame.put(row, width - 1, Glyphs::BoxSide, border);
        if (index == 0 || index + 1 == panelRows) {
            continue;  // A row of panel and nothing else.
        }

        const std::size_t line = index - 1;
        if (line == 0) {
            frame.put(row, markerColumn, Glyphs::PromptMarker,
                      Palette::accent().withBackground(Palette::panelColor()));
        }

        const std::size_t rangeStart = prompt.ranges[line].first;
        const std::string& text = prompt.rows[line];
        const std::size_t token =
            commandLength > rangeStart ? std::min(commandLength - rangeStart, text.size()) : 0;
        if (token > 0) {
            frame.put(row, textColumn, std::string_view{text}.substr(0, token),
                      Palette::accent().withBackground(Palette::panelColor()));
        }
        frame.put(row, textColumn + token, std::string_view{text}.substr(token),
                  Palette::text().withBackground(Palette::panelColor()));
        if (line == 0 && model.draft.empty()) {
            frame.put(row, textColumn, placeholderOf(model),
                      Palette::muted().withBackground(Palette::panelColor()));
        }
    }
    paintBoxBorder(frame, firstRow + panelRows + 1, width, false);
}

}  // namespace

RenderResult render(const Model& model, std::size_t width, std::size_t height,
                    const Selection& selection) {
    RenderResult result{Canvas{width, height}, 0, 0};
    Canvas& frame = result.frame;
    if (width == 0 || height == 0) {
        return result;
    }

    // The prompt has a frame of its own, so the line being typed is not read as one more row of
    // the conversation, and the line inside it is laid out to the width the column of the marker
    // and the panel and the stroke on the right leave.
    const std::size_t textColumn = PanelPad + PromptWidth;
    const std::size_t lastColumn =
        width > (BoxWidth + PanelPad) ? width - BoxWidth - PanelPad - 1 : 0;
    const std::size_t textColumns = lastColumn >= textColumn ? lastColumn - textColumn + 1 : 1;
    const DraftLayout prompt = layoutDraft(model.draft, textColumns);

    const std::vector<std::string> all = App::hintsFor(model);
    const std::size_t selected = all.empty() ? 0 : std::min(model.hintIndex, all.size() - 1);
    std::size_t first = 0;
    const std::vector<std::string> hints = hintWindow(all, selected, first);

    // The frame takes two rows of its own, and the panel inside it one above the line being
    // typed and one below. The status row goes under the whole thing.
    const std::size_t boxRows = prompt.rows.size() + 4;
    const bool withStatus = height > boxRows + hints.size();
    const std::size_t bottomRows = boxRows + hints.size() + (withStatus ? 1 : 0);
    const std::size_t viewRows = height > bottomRows ? height - bottomRows : 0;

    // The transcript, and the column the scrollbar takes when the conversation does not fit.
    // Laying it out again one column narrower is what keeps the bar off the last character.
    std::vector<TranscriptRow> transcript = transcriptRows(model, width);
    if (width > 1 && viewRows > 0 && transcript.size() > viewRows) {
        transcript = transcriptRows(model, width - 1);
    }

    // The viewport of the conversation, scrolled by the model.
    const std::size_t total = transcript.size();
    const std::size_t range = total > viewRows ? total - viewRows : 0;
    result.maxScrollOffset = range;
    std::size_t start = 0;
    if (range > 0) {
        start = range - std::min(model.scrollOffset, range);
    }
    for (std::size_t row = 0; row < viewRows; ++row) {
        if (row < total) {
            const TranscriptRow& line = transcript[start + row];
            frame.put(row, 0, line.prefix, line.prefixStyle);
            frame.put(row, PrefixWidth, line.body, line.bodyStyle);
        }
    }
    paintScrollBar(frame, viewRows, width, total, start);

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

    // The prompt, framed, which is the part that stays put while the conversation moves.
    paintPrompt(frame, model, prompt, row, width);
    row += prompt.rows.size() + 4;

    if (withStatus) {
        frame.put(row, 0, statusRow(model), Palette::muted());
    }

    const auto position = prompt.positions.at(std::min(model.cursor, model.draft.size()));
    result.caretRow = viewRows + hints.size() + 2 + position.first;
    result.caretColumn = PanelPad + PromptWidth + position.second;

    pickOutSelection(frame, selection);
    return result;
}

}  // namespace OpenAnimaTui::Screen
