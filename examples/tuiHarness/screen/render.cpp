#include "render.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
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
using Metrics::HintPad;
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

/// Adds a note, which belongs to no speaker: it starts at the left edge.
/// @param rows Rows collected so far, appended to.
/// @param text Note to lay out.
/// @param width Columns available for the body.
/// @param style How it is painted.
void appendNote(std::vector<TranscriptRow>& rows, const std::string& text, std::size_t width,
                const Style& style) {
    for (const std::string& line : wrap(text, width)) {
        rows.push_back({{}, line, style, style});
    }
}

/// Adds the blank row that keeps a message from running into the one before it.
/// @param rows Rows collected so far, appended to.
void appendGap(std::vector<TranscriptRow>& rows) {
    rows.push_back({{}, {}, Palette::text(), Palette::text()});
}

/// Adds the rows of one message: a note at the left edge, or a line under the name of whoever
/// is speaking, which only the first line of a block carries.
/// @param rows Rows collected so far, appended to.
/// @param message Message to lay out.
/// @param npc Name the active npc is shown under.
/// @param width Columns available.
void appendTurn(std::vector<TranscriptRow>& rows, const Message& message, const std::string& npc,
                std::size_t width) {
    const std::size_t bodyWidth = width > PrefixWidth ? width - PrefixWidth : 1;
    if (message.role == Role::Speaker) {
        // The name over what follows, painted like the name of a speaker: a raw turn is
        // credited to the npc that wrote it without the document reading as something it said.
        appendNote(rows, message.text, width, Palette::primary());
        return;
    }
    if (message.role == Role::System) {
        if (message.continues) {
            // An action line is part of the reply above it, so it lines up under it.
            appendMessage(rows, message, {}, Palette::muted(), Palette::muted(), bodyWidth);
            return;
        }
        appendNote(rows, message.text, width, Palette::muted());
        return;
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
        appendTurn(rows, message, npc, width);
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
/// screen, not about the conversation behind it. Only what was said is painted: the panel, the
/// strokes of the prompt frame, the marker and the scrollbar keep the way they look, so a pick
/// that crosses them marks the words and nothing else. A drag that is down to one cell still
/// shows, which is why this asks whether the mouse is holding something and not whether there is
/// something worth copying.
/// @param frame Frame to paint on.
/// @param selection Cells the mouse picked out.
/// @param rows Rows of the screen the scope of the selection takes: nothing outside them is
///        painted, so a pick of the conversation never marks the prompt and one of the prompt
///        never marks what is said above it.
void pickOutSelection(Canvas& frame, const Selection& selection, Bounds rows) {
    if (!selection.active() || rows.empty() || frame.height() == 0 || selection.lastRow() < 0) {
        return;
    }
    const std::size_t first = std::max(
        static_cast<std::size_t>(std::max<std::ptrdiff_t>(selection.firstRow(), 0)), rows.first);
    const std::size_t last =
        std::min(static_cast<std::size_t>(std::max<std::ptrdiff_t>(selection.lastRow(), 0)),
                 std::min(rows.end(), frame.height()) - 1);
    if (first > last) {
        return;
    }
    for (std::size_t picked = first; picked <= last; ++picked) {
        for (std::size_t column = 0; column < frame.width(); ++column) {
            if (!selection.contains(picked, column)) {
                continue;
            }
            Cell& cell = frame.at(picked, column);
            if (isLayout(frame, cell, column)) {
                continue;
            }
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

/// Where a bar goes and what it stands for.
struct BarLayout {
    std::size_t firstRow{0};  ///< Row its track starts on.
    std::size_t rows{0};      ///< Rows the track takes.
    std::size_t column{0};    ///< Column it is painted in.
    std::size_t total{0};     ///< Rows the whole thing takes.
    std::size_t start{0};     ///< First row that is shown.
    Style track;              ///< How the track is painted.
    Style thumb;              ///< How the thumb is painted.
    Style lit;                ///< How the thumb is painted while the pointer is over the bar.
};

/// Paints a scrollbar: one column, a picture of where the viewport sits and nothing else. The
/// track is dim, the thumb is not, and the thumb is painted in the accent colour while the
/// pointer is over the bar, which is what says it can be moved.
/// @param frame Canvas to paint on.
/// @param bar Where it goes and what it stands for.
/// @param hovered Whether the pointer is over the bar.
void paintBar(Canvas& frame, const BarLayout& bar, bool hovered) {
    if (bar.total <= bar.rows || bar.rows == 0 || bar.column >= frame.width()) {
        return;
    }

    for (std::size_t row = 0; row < bar.rows; ++row) {
        frame.put(bar.firstRow + row, bar.column, Glyphs::ScrollBar, bar.track);
    }

    const std::size_t thumb = std::max<std::size_t>(1, bar.rows * bar.rows / bar.total);
    const std::size_t top = thumbRow(bar.total, bar.rows, bar.start);
    for (std::size_t row = 0; row < thumb && top + row < bar.rows; ++row) {
        frame.put(bar.firstRow + top + row, bar.column, Glyphs::ScrollBar,
                  hovered ? bar.lit : bar.thumb);
    }

    // A bar is the client's own drawing, so neither a copy nor a selection takes it for
    // something that was said.
    for (std::size_t row = 0; row < bar.rows; ++row) {
        frame.at(bar.firstRow + row, bar.column).furniture = true;
    }
}

/// Paints one horizontal border of the frame around the prompt.
///
/// The stroke runs along the inside edge of the panel and the corners join it to the side ones, so
/// the border is the first pixel of the colour and there is nothing of the panel outside it.
/// @param frame Canvas to paint on.
/// @param row Row to paint.
/// @param width Columns available.
/// @param top Whether it is the upper border.
// A row and a width are both counts, and both are named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void paintBoxBorder(Canvas& frame, std::size_t row, std::size_t width, bool top) {
    if (width == 0) {
        return;
    }
    const Style border = Palette::muted().withBackground(Palette::panelColor());
    frame.put(row, 0, top ? Glyphs::BoxTopLeft : Glyphs::BoxBottomLeft, border);
    for (std::size_t column = 1; column + 1 < width; ++column) {
        frame.put(row, column, top ? Glyphs::BoxTop : Glyphs::BoxBottom, border);
    }
    frame.put(row, width - 1, top ? Glyphs::BoxTopRight : Glyphs::BoxBottomRight, border);
}

/// What the empty prompt says: what can be done from here, which is not the same in the lobby
/// as in a conversation.
/// @param model Model to read.
/// @return The text, which the prompt draws in the quiet colour.
std::string placeholderOf(const Model& model) {
    return model.world.active.has_value() ? std::string{"say something, or /help"}
                                          : std::string{"/create <description> to make an npc"};
}

/// The window of the draft the panel shows.
struct PromptWindow {
    std::size_t rows{0};   ///< Rows of the draft the panel shows.
    std::size_t first{0};  ///< Row of the draft the window starts on.
};

/// The box can only be as tall as the screen, so a draft taller than that is shown through a
/// window that keeps the caret inside it, or that stays where the reader scrolled it to.
/// @param total Rows the draft takes.
/// @param caret Row of the draft the caret is on.
/// @param height Rows the screen has.
/// @param scrolled First row of the draft the reader scrolled to, when there is one.
/// @return The rows of the draft the panel shows, and the first one of them.
// Three counts and a position, each named where it is used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
PromptWindow promptWindow(std::size_t total, std::size_t caret, std::size_t height,
                          const std::optional<std::size_t>& scrolled) {
    const std::size_t room = height > 3 ? height - 3 : 1;
    const std::size_t rows = std::min(total, room);
    if (rows == 0) {
        return PromptWindow{0, 0};
    }
    if (scrolled.has_value()) {
        return PromptWindow{rows, std::min(*scrolled, total - rows)};
    }
    const std::size_t first = caret + 1 > rows ? caret + 1 - rows : 0;
    return PromptWindow{rows, std::min(first, total - rows)};
}

/// Paints the rows of the conversation that are on the screen.
/// @param frame Canvas to paint on.
/// @param rows The whole conversation, laid out.
/// @param start First row of it that is on screen.
/// @param viewRows Rows the viewport has.
void paintTranscript(Canvas& frame, const std::vector<TranscriptRow>& rows, std::size_t start,
                     std::size_t viewRows) {
    for (std::size_t row = 0; row < viewRows && start + row < rows.size(); ++row) {
        const TranscriptRow& line = rows[start + row];
        // The name is what was said; the padding that lines the body up under it is layout, so
        // it is left blank and a copy does not carry it.
        std::string_view name = line.prefix;
        while (!name.empty() && name.back() == ' ') {
            name.remove_suffix(1);
        }
        frame.put(row, 0, name, line.prefixStyle);
        if (!name.empty()) {
            frame.put(row, name.size(), " ", line.prefixStyle);
        }
        frame.put(row, measure(line.prefix), line.body, line.bodyStyle);
    }
}

/// @param frame Frame on screen.
/// @param position Where the caret is in the draft.
/// @param window Rows of the draft the panel shows.
/// @param viewRows Rows the conversation takes.
/// @return Row the caret goes on, or one past the screen when it is out of the window.
std::size_t caretRowOf(const Canvas& frame, const std::pair<std::size_t, std::size_t>& position,
                       const PromptWindow& window, std::size_t viewRows) {
    const bool shown =
        position.first >= window.first && position.first < window.first + window.rows;
    return shown ? viewRows + 1 + (position.first - window.first) : frame.height();
}

/// Paints the prompt inside the frame that holds it, the frame's top border included.
///
/// The panel shows a window of the rows of the draft, since the box can only be as tall as the
/// screen: the caret is always inside the window, so what is being typed never leaves the
/// screen, and the bar in the padding column says how much of the draft is out of it.
/// @param frame Canvas to paint on.
/// @param model Model to read the draft and the placeholder from.
/// @param prompt The draft as it was laid out.
/// @param firstRow Row the top border goes on.
/// @param width Columns available.
/// @param firstShown Row of the draft that the window starts on.
/// @param shownRows Rows the window takes.
/// @param pointer Where the pointer is, which decides whether the bar is lit up.
// The row of the border, the width and the rows of the window are all counts, and they are
// named where they are used.
void paintPrompt(Canvas& frame, const Model& model, const DraftLayout& prompt, std::size_t firstRow,
                 std::size_t width,  // NOLINT(bugprone-easily-swappable-parameters)
                 std::size_t firstShown, std::size_t shownRows, const Pointer& pointer) {
    const std::size_t markerColumn = BoxWidth + PanelPad;
    const std::size_t textColumn = markerColumn + PromptWidth;
    const std::size_t commandLength = commandTokenLength(model.draft);
    const Style border = Palette::muted().withBackground(Palette::panelColor());
    // The columns the line has once the frame and the padding on both sides are taken out.
    const std::size_t textColumns =
        width > textColumn + BoxWidth + PanelPad ? width - textColumn - BoxWidth - PanelPad : 0;

    // The panel is the box and the frame runs along its inside edge: the strokes are the first
    // pixel of colour on every side, so the colour does not show outside them and the text sits
    // in a band of it. The panel is as tall as the draft gets on the screen, with a column of it
    // on each side.
    const std::size_t boxRows = shownRows + 2;
    if (width > 0) {
        for (std::size_t row = firstRow; row < firstRow + boxRows; ++row) {
            frame.fill(row, 0, width, Palette::panel());
        }
    }

    paintBoxBorder(frame, firstRow, width, true);
    for (std::size_t index = 0; index < shownRows; ++index) {
        const std::size_t source = firstShown + index;
        const std::size_t row = firstRow + 1 + index;
        frame.put(row, 0, Glyphs::BoxLeft, border);
        frame.put(row, width - 1, Glyphs::BoxRight, border);
        if (source == 0) {
            frame.put(row, markerColumn, Glyphs::PromptMarker,
                      Palette::accent().withBackground(Palette::panelColor()));
        }

        const std::size_t rangeStart = prompt.ranges[source].first;
        const std::string& text = prompt.rows[source];
        const std::size_t token =
            commandLength > rangeStart ? std::min(commandLength - rangeStart, text.size()) : 0;
        if (token > 0) {
            frame.put(row, textColumn, std::string_view{text}.substr(0, token),
                      Palette::accent().withBackground(Palette::panelColor()));
        }
        frame.put(row, textColumn + token, std::string_view{text}.substr(token),
                  Palette::text().withBackground(Palette::panelColor()));
        if (source == 0 && model.draft.empty()) {
            // What the empty prompt says is a line of its own: it is cut with an ellipsis rather
            // than allowed to run over the frame when the window is narrow.
            frame.put(row, textColumn, clip(placeholderOf(model), textColumns),
                      Palette::muted().withBackground(Palette::panelColor()));
        }
    }
    paintBoxBorder(frame, firstRow + shownRows + 1, width, false);

    // The bar goes in the padding column inside the frame, which is empty anyway, so the draft
    // keeps the columns it had. It is only there when the draft is taller than the panel.
    const std::size_t barColumn = width > BoxWidth + PanelPad ? width - BoxWidth - PanelPad : 0;
    const bool hovered = pointer.onScreen && pointer.column == barColumn &&
                         pointer.row > firstRow && pointer.row <= firstRow + shownRows;
    paintBar(frame,
             BarLayout{firstRow + 1, shownRows, barColumn, prompt.rows.size(), firstShown,
                       Palette::muted().withDim().withBackground(Palette::panelColor()),
                       Palette::muted().withBackground(Palette::panelColor()),
                       Palette::accent().withBackground(Palette::panelColor()).withBold()},
             hovered);
}

/// Where the panel of hints goes and what it holds.
struct HintLayout {
    std::size_t selected{0};  ///< Which hint is selected, counted in the whole list.
    std::size_t first{0};     ///< Index in the whole list of the first hint of the window.
    std::size_t total{0};     ///< Hints that match.
    std::size_t firstRow{0};  ///< Row the first one goes on.
    std::size_t columns{0};   ///< Columns the panel takes.
};

/// Columns the panel of hints takes.
///
/// The panel is as wide as the longest command, its indent and the margin after it, which is
/// where the bar goes when the list does not fit the panel. A screen narrower than that keeps
/// the panel inside itself: it takes what there is, and the commands that no longer fit whole
/// are cut with an ellipsis.
/// @param hints Hints that match, whether the panel shows them or not.
/// @param width Columns of the screen.
/// @return The columns of the panel, never wider than the screen.
[[nodiscard]] std::size_t hintPanelColumns(const std::vector<std::string>& hints,
                                           std::size_t width) {
    std::size_t longest = 0;
    for (const std::string& hint : hints) {
        longest = std::max(longest, measure(hint));
    }
    return std::min(width, HintIndent + longest + HintPad);
}

/// Paints the panel of hints: the commands that complete what is being typed, floating over the
/// last rows of the conversation, anchored to the left edge.
///
/// The panel is not as wide as the screen on purpose: it covers a share of it, so the
/// conversation stays readable beside it. The selected hint is in the accent colour, the one
/// under the pointer is lit up the same way with the underline and the weight that say a click
/// would write it, and the bar on its right edge scrolls a list longer than the panel.
/// @param frame Canvas to paint on.
/// @param hints Hints of the window, in order.
/// @param layout Where the panel goes and what it holds.
/// @param pointer Where the pointer is.
void paintHints(Canvas& frame, const std::vector<std::string>& hints, const HintLayout& layout,
                const Pointer& pointer) {
    const std::size_t rows = hints.size();
    if (rows == 0 || layout.columns < 2) {
        return;
    }
    const std::size_t barColumn = layout.columns - 1;
    const bool scrolls = layout.total > rows;
    const bool barUnderPointer = pointer.onScreen && pointer.column + 1 == layout.columns &&
                                 pointer.row >= layout.firstRow &&
                                 pointer.row < layout.firstRow + rows;
    const std::size_t textColumns = barColumn > HintIndent ? barColumn - HintIndent : 0;

    for (std::size_t index = 0; index < rows; ++index) {
        const std::size_t row = layout.firstRow + index;
        Style style =
            (layout.first + index == layout.selected ? Palette::accent() : Palette::muted())
                .withBackground(Palette::raisedColor());
        if (pointer.onScreen && pointer.row == row && pointer.column + 1 < layout.columns) {
            style =
                Palette::accent().withBackground(Palette::raisedColor()).withBold().withUnderline();
        }
        frame.fill(row, 0, layout.columns, Palette::raised());
        frame.put(row, HintIndent, clip(hints[index], textColumns), style);
    }

    if (scrolls) {
        for (std::size_t index = 0; index < rows; ++index) {
            frame.put(layout.firstRow + index, barColumn, Glyphs::ScrollBar,
                      Palette::muted().withDim());
        }
        const std::size_t thumb = std::max<std::size_t>(1, rows * rows / layout.total);
        const std::size_t top = hintThumbRow(layout.total, rows, layout.selected);
        const Style style = barUnderPointer ? Palette::accent().withBold() : Palette::muted();
        for (std::size_t index = 0; index < thumb && top + index < rows; ++index) {
            frame.put(layout.firstRow + top + index, barColumn, Glyphs::ScrollBar, style);
        }
    }

    // The panel is the client's own drawing, so neither a copy nor a selection takes it for
    // something that was said.
    for (std::size_t index = 0; index < rows; ++index) {
        for (std::size_t column = 0; column < layout.columns; ++column) {
            frame.at(layout.firstRow + index, column).furniture = true;
        }
    }
}

}  // namespace

std::size_t thumbRow(std::size_t transcriptRows, std::size_t viewRows, std::size_t start) {
    if (transcriptRows <= viewRows || viewRows == 0) {
        return 0;
    }
    const std::size_t thumb = std::max<std::size_t>(1, viewRows * viewRows / transcriptRows);
    const std::size_t travel = viewRows - thumb;
    if (travel == 0) {
        return 0;  // The thumb fills the bar, so there is nowhere for it to go.
    }
    return start * travel / (transcriptRows - viewRows);
}

// The two counts and the row are named where they are used, and both helpers take them in the
// order the bar is laid out in.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::size_t firstRowForThumb(std::size_t transcriptRows, std::size_t viewRows, std::size_t row) {
    if (transcriptRows <= viewRows || viewRows == 0) {
        return 0;
    }
    const std::size_t thumb = std::max<std::size_t>(1, viewRows * viewRows / transcriptRows);
    const std::size_t travel = viewRows - thumb;
    if (travel == 0) {
        return 0;
    }
    const std::size_t wanted = std::min(row, travel);
    return std::min(transcriptRows - viewRows,
                    (wanted * (transcriptRows - viewRows) + travel - 1) / travel);
}

// As above: counts and a row, all named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::size_t hintThumbRow(std::size_t total, std::size_t rows, std::size_t selected) {
    if (total <= rows || rows == 0 || total < 2) {
        return 0;
    }
    const std::size_t thumb = std::max<std::size_t>(1, rows * rows / total);
    const std::size_t travel = rows - thumb;
    if (travel == 0) {
        return 0;
    }
    return std::min(selected, total - 1) * travel / (total - 1);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::size_t hintAtThumbRow(std::size_t total, std::size_t rows, std::size_t row) {
    if (total <= rows || rows == 0 || total < 2) {
        return 0;
    }
    const std::size_t thumb = std::max<std::size_t>(1, rows * rows / total);
    const std::size_t travel = rows - thumb;
    if (travel == 0) {
        return 0;
    }
    const std::size_t wanted = std::min(row, travel);
    return std::min(total - 1, (wanted * (total - 1) + travel / 2) / travel);
}

RenderResult render(const Model& model, std::size_t width, std::size_t height,
                    const Selection& selection, const Pointer& pointer) {
    RenderResult result{Canvas{width, height}, 0, 0};
    Canvas& frame = result.frame;
    if (width == 0 || height == 0) {
        return result;
    }

    // The prompt has a frame of its own, so the line being typed is not read as one more row of
    // the conversation, and the line inside it is laid out to the width the frame and the panel
    // around it leave.
    const std::size_t textColumn = BoxWidth + PanelPad + PromptWidth;
    const std::size_t lastColumn =
        width > (BoxWidth + PanelPad) ? width - BoxWidth - PanelPad - 1 : 0;
    const std::size_t textColumns = lastColumn >= textColumn ? lastColumn - textColumn + 1 : 1;
    const DraftLayout prompt = layoutDraft(model.draft, textColumns);

    // The caret decides which rows of the draft the panel shows, unless the reader scrolled the
    // window away from it, which is what the bar of the prompt is for.
    const std::size_t caret = std::min(model.cursor, model.draft.size());
    const auto position = prompt.positions.at(caret);
    const PromptWindow window =
        promptWindow(prompt.rows.size(), position.first, height, model.promptScroll);

    const std::vector<std::string> all = App::hintsFor(model);
    const std::size_t selected = all.empty() ? 0 : std::min(model.hintIndex, all.size() - 1);
    std::size_t first = 0;
    const std::vector<std::string> hints = hintWindow(all, selected, first);

    // The frame takes two rows of its own: the line being typed is the panel inside it, and the
    // status row goes under the whole thing. The box grows with the draft until it takes the
    // screen, and the status keeps its row while there is room. The hints float over the
    // conversation, so they do not take rows of their own.
    const std::size_t boxRows = window.rows + 2;
    const bool withStatus = height > boxRows;
    const std::size_t bottomRows = boxRows + (withStatus ? 1 : 0);
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
    paintTranscript(frame, transcript, start, viewRows);
    if (width > 1) {
        paintBar(frame,
                 BarLayout{0, viewRows, width - 1, total, start, Palette::muted().withDim(),
                           Palette::muted(), Palette::accent().withBold()},
                 pointer.onScreen && pointer.column + 1 == width && pointer.row < viewRows);
    }
    result.scrollRows = viewRows;
    result.transcriptRows = total;

    // The hints float over the last rows of the conversation, anchored to the left edge, on a
    // panel of their own that leaves the rest of the screen to what was said.
    const std::size_t hintColumns = hintPanelColumns(all, width);
    const std::size_t hintRows = hints.size();
    const std::size_t hintTop = viewRows > hintRows ? viewRows - hintRows : 0;
    result.hintRow = hintTop;
    result.hintCount = hintRows;
    result.hintOffset = first;
    result.hintColumns = hintColumns;
    result.hintTotal = all.size();
    paintHints(frame, hints, HintLayout{selected, first, all.size(), hintTop, hintColumns},
               pointer);

    // The prompt, framed, which is the part that stays put while the conversation moves.
    std::size_t row = viewRows;
    paintPrompt(frame, model, prompt, row, width, window.first, window.rows, pointer);
    row += window.rows + 2;
    result.promptFirst = window.first;
    result.promptRows = window.rows;
    result.promptTotal = prompt.rows.size();

    if (withStatus) {
        // The status is the last row, but it is cut all the same: it ends in an ellipsis rather
        // than in half a sentence running off the screen.
        frame.put(row, 0, clip(statusRow(model), width), Palette::muted());
    }

    // The caret is only placed where the terminal can show it: while the panel is scrolled away
    // from it there is nothing to place, and the writer leaves the cursor where it is.
    result.caretRow = caretRowOf(frame, position, window, viewRows);
    result.caretColumn = BoxWidth + PanelPad + PromptWidth + position.second;

    // What was picked out is painted inside the rows of its scope: a span of the conversation
    // is never painted over the prompt, however far the text under it has scrolled, and a span
    // of the prompt is never painted over the conversation it was dragged across.
    pickOutSelection(frame, selection, boundsOf(selection.scope(), viewRows, frame.height()));
    return result;
}

}  // namespace OpenAnimaTui::Screen
