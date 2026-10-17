#include "interaction.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "domain/model.h"
#include "presentation/render.h"
#include "presentation/selection.h"
#include "terminal/terminal.h"

namespace OpenAnimaTui::Application {

namespace {

/// Rows at an edge of the viewport that, while a selection is dragged over them, move the
/// conversation on their own.
constexpr std::size_t AutoScrollMargin = 1;

/// How long a drag waiting at an edge takes to move another row.
constexpr std::chrono::milliseconds AutoScrollStep{50};

/// @param kind Key that arrived.
/// @return Whether it moves the transcript, which takes what was picked out with it.
bool scrollsTranscript(Key::Kind kind) {
    return kind == Key::Kind::WheelUp || kind == Key::Kind::WheelDown ||
           kind == Key::Kind::PageUp || kind == Key::Kind::PageDown;
}

/// @param interaction What the pointer has done to the screen.
/// @param column Column the pointer is on.
/// @param row Row the pointer is on.
/// @return Whether the pointer is over the bar of the conversation.
bool overScrollBar(const Interaction& interaction, std::size_t column, std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    return shape.maxScrollOffset > 0 && row < shape.scrollRows && column + 1 == shape.frame.width();
}

/// @param interaction What the pointer has done to the screen.
/// @param column Column the pointer is on.
/// @param row Row the pointer is on.
/// @return Whether the pointer is over the bar of the prompt, which is only there when the
///         draft is taller than the panel that shows it.
bool overPromptBar(const Interaction& interaction, std::size_t column, std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    const std::size_t width = shape.frame.width();
    if (shape.promptTotal <= shape.promptRows || shape.promptRows == 0 ||
        width < Presentation::Metrics::BoxWidth + Presentation::Metrics::PanelPad) {
        return false;
    }
    const std::size_t barColumn =
        width - Presentation::Metrics::BoxWidth - Presentation::Metrics::PanelPad;
    return column == barColumn && row > shape.scrollRows &&
           row <= shape.scrollRows + shape.promptRows;
}

/// @param interaction What the pointer has done to the screen.
/// @param row Row the pointer is on.
/// @return Whether the pointer is over the box of the prompt, borders included.
bool overPromptBox(const Interaction& interaction, std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    return row >= shape.scrollRows && row < shape.scrollRows + shape.promptRows + 2;
}

/// Puts the panel of the prompt where the pointer puts its thumb.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, which holds where the panel starts.
/// @param row Row the pointer is on.
void dragPromptBarTo(const Interaction& interaction, Model& model, std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    const std::size_t top = shape.scrollRows + 1;
    const std::size_t offset = row >= top ? row - top : 0;
    model.promptScroll =
        Presentation::firstRowForThumb(shape.promptTotal, shape.promptRows, offset);
}

/// @param interaction What the pointer has done to the screen.
/// @param column Column the pointer is on.
/// @param row Row the pointer is on.
/// @return Whether the pointer is over the bar of the hints, which is only there when the list
///         is longer than the panel.
bool overHintBar(const Interaction& interaction, std::size_t column, std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    return shape.hintTotal > shape.hintCount && shape.hintCount > 0 &&
           column + 1 == shape.hintColumns && row >= shape.hintRow &&
           row < shape.hintRow + shape.hintCount;
}

/// @param interaction What the pointer has done to the screen.
/// @param column Column the pointer is on.
/// @param row Row the pointer is on.
/// @return The hint the pointer is over, or nothing when it is not on the panel.
std::optional<std::size_t> hintAt(const Interaction& interaction, std::size_t column,
                                  std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    if (column + 1 > shape.hintColumns || row < shape.hintRow ||
        row >= shape.hintRow + shape.hintCount) {
        return std::nullopt;
    }
    return shape.hintOffset + (row - shape.hintRow);
}

/// Walks the hint selection, stopping at the ends of the list.
/// @param model The model, which holds the selection.
/// @param forward Whether it goes on to the next hint or back to the one before.
void stepHint(Model& model, bool forward) {
    const std::size_t count = Domain::hintsFor(model).size();
    if (count == 0) {
        return;
    }
    const std::size_t index = std::min(model.hintIndex, count - 1);
    if (!forward) {
        model.hintIndex = index > 0 ? index - 1 : 0;
        return;
    }
    model.hintIndex = std::min(index + 1, count - 1);
}

/// Puts the thumb of the hints where the pointer is.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, which holds the selection.
/// @param row Row the pointer is on.
void dragHintBarTo(const Interaction& interaction, Model& model, std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    const std::size_t offset = row >= shape.hintRow ? row - shape.hintRow : 0;
    model.hintIndex = Presentation::hintAtThumbRow(shape.hintTotal, shape.hintCount, offset);
}

/// Puts the thumb of the bar where the pointer is, keeping the row it was grabbed by.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, which holds the offset.
/// @param row Row the pointer is on.
void dragBarTo(Interaction& interaction, Model& model, std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    const std::size_t range = shape.maxScrollOffset;
    const std::size_t viewRows = shape.scrollRows;
    if (range == 0 || Presentation::thumbRow(shape.transcriptRows, viewRows, range) == 0) {
        return;  // There is no room for the thumb to move, so there is nothing to drag.
    }
    const std::size_t top = row >= interaction.barGrab ? row - interaction.barGrab : 0;
    const std::size_t start = Presentation::firstRowForThumb(shape.transcriptRows, viewRows, top);
    model.scrollOffset = range - std::min(start, range);
}

/// Takes hold of the bar: where along the thumb it was grabbed decides how it follows, and a
/// press beside the thumb brings it under the pointer.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, which holds the offset.
/// @param row Row the pointer is on.
void grabBar(Interaction& interaction, Model& model, std::size_t row) {
    const Presentation::RenderResult& shape = interaction.shape;
    const std::size_t range = shape.maxScrollOffset;
    const std::size_t viewRows = shape.scrollRows;
    const std::size_t travel = Presentation::thumbRow(shape.transcriptRows, viewRows, range);
    const std::size_t thumb = viewRows > travel ? viewRows - travel : 1;
    const std::size_t start = range - std::min(model.scrollOffset, range);
    const std::size_t top = Presentation::thumbRow(shape.transcriptRows, viewRows, start);
    interaction.barGrab = (row >= top && row < top + thumb) ? row - top : thumb / 2;
    interaction.drag = Interaction::Drag::ConversationBar;
    dragBarTo(interaction, model, row);
}

/// Watches the pointer cross the screen, which is what a hover is.
/// @param interaction What the pointer has done to the screen.
/// @param row Row the pointer is on.
/// @param column Column the pointer is on.
// A row and a column, both named where they are used.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void movePointer(Interaction& interaction, std::size_t row, std::size_t column) {
    interaction.pointer.onScreen = true;
    interaction.pointer.row = row;
    interaction.pointer.column = column;
}

/// @param interaction What the pointer has done to the screen.
/// @param row Row the pointer is on.
/// @return Where a selection made on that row lives: the conversation scrolls under it, and
///         the prompt and what surrounds it do not.
Presentation::Scope scopeAt(const Interaction& interaction, std::size_t row) {
    return row < interaction.shape.scrollRows ? Presentation::Scope::Conversation
                                              : Presentation::Scope::Screen;
}

/// @param interaction What the pointer has done to the screen.
/// @param selection What is picked out.
/// @return Rows of the screen the scope of the selection takes, which is as far as it reaches.
Presentation::Bounds rowsOf(const Interaction& interaction,
                            const Presentation::Selection& selection) {
    return Presentation::boundsOf(selection.scope(), interaction.shape.scrollRows,
                                  interaction.shape.frame.height());
}

/// Copies what the mouse picked out, to the terminal and to hand for a later paste.
/// @param interaction What the pointer has done to the screen.
/// @param painted The frame on screen, which is where the text is read from.
/// @param clipboard Where the text is kept for the next paste.
/// @param terminal Terminal the clipboard sequence goes to.
void copySelection(const Interaction& interaction, const Presentation::Canvas& painted,
                   std::string& clipboard, Terminal& terminal) {
    clipboard = Presentation::selectedText(painted, interaction.selection,
                                           rowsOf(interaction, interaction.selection));
    if (clipboard.empty()) {
        return;
    }
    terminal.copyToClipboard(clipboard);
}

/// Feeds text into the draft as if it had been typed, line breaks included.
/// @param model The model.
/// @param text Text to insert.
/// @param scrollStep Rows a page scroll takes, as `update` wants.
void insertText(Model& model, const std::string& text, std::size_t scrollStep) {
    for (const char value : text) {
        const Key key =
            value == '\n' ? Key{Key::Kind::CtrlEnter, 0} : Key{Key::Kind::Character, value};
        (void)Domain::update(model, key, scrollStep);
    }
}

/// Where a drag that waits at an edge wants the view to go.
enum class EdgeScroll : std::uint8_t {
    None,     ///< No selection is dragged against an edge.
    Earlier,  ///< The pointer is at the top: the view moves towards the beginning.
    Later     ///< The pointer is at the bottom: it moves towards the end.
};

/// What a drag at an edge moves.
enum class EdgeTarget : std::uint8_t {
    None,          ///< Nothing is dragged against an edge.
    Conversation,  ///< The transcript, which scrolls under a pick of the viewport.
    Prompt         ///< The draft, which scrolls under a pick of the panel that shows it.
};

/// What a drag that waits at an edge is asking for.
struct EdgeDrag {
    EdgeTarget target{EdgeTarget::None};     ///< Which part of the screen moves.
    EdgeScroll direction{EdgeScroll::None};  ///< Which way it goes.
};

/// @param interaction What the pointer has done to the screen.
/// @return What the drag is pushing, which is nothing when the pointer is not holding a
///         selection against an edge of the part it belongs to.
EdgeDrag edgeDragOf(const Interaction& interaction) {
    if (interaction.drag != Interaction::Drag::Selection || interaction.selection.empty()) {
        return {};
    }
    const Presentation::RenderResult& shape = interaction.shape;
    if (interaction.selection.scope() == Presentation::Scope::Conversation) {
        if (shape.scrollRows == 0) {
            return {};
        }
        if (interaction.pointer.row < AutoScrollMargin) {
            return {EdgeTarget::Conversation, EdgeScroll::Earlier};
        }
        if (interaction.pointer.row + AutoScrollMargin >= shape.scrollRows) {
            return {EdgeTarget::Conversation, EdgeScroll::Later};
        }
        return {};
    }
    // A selection that is not the conversation's is the prompt's, and the panel of the prompt is
    // the only part of it that can move: the rows above and below it are the box's.
    if (shape.promptRows == 0 || shape.promptTotal <= shape.promptRows) {
        return {};
    }
    const std::size_t top = shape.scrollRows + 1;
    const std::size_t bottom = shape.scrollRows + shape.promptRows;
    if (interaction.pointer.row <= top) {
        return {EdgeTarget::Prompt, EdgeScroll::Earlier};
    }
    if (interaction.pointer.row >= bottom) {
        return {EdgeTarget::Prompt, EdgeScroll::Later};
    }
    return {};
}

/// Moves the conversation a row, taking the end of the selection that is tied to the text with
/// it while the end under the pointer stays where it is.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, which holds the offset.
/// @param direction Where the conversation goes.
/// @return Whether the conversation moved.
bool autoScrollConversation(Interaction& interaction, Model& model, EdgeScroll direction) {
    const std::size_t before = model.scrollOffset;
    if (direction == EdgeScroll::Earlier) {
        model.scrollOffset = std::min(before + 1, interaction.shape.maxScrollOffset);
    } else if (before > 0) {
        model.scrollOffset = before - 1;
    }
    if (model.scrollOffset == before) {
        return false;
    }
    interaction.selection.shift(static_cast<std::ptrdiff_t>(model.scrollOffset) -
                                static_cast<std::ptrdiff_t>(before));
    // The pointer has not moved, so only the end tied to the text follows it: the moving end
    // goes back to the cell the mouse is waiting on.
    interaction.selection.extend(interaction.pointer.row, interaction.pointer.column);
    return true;
}

/// Moves the panel of the prompt a row, taking the end of the selection that is tied to the
/// draft with it while the end under the pointer stays where it is.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, which holds where the panel starts.
/// @param direction Where the draft goes.
/// @return Whether the panel moved.
bool autoScrollPrompt(Interaction& interaction, Model& model, EdgeScroll direction) {
    const Presentation::RenderResult& shape = interaction.shape;
    const std::size_t last = shape.promptTotal - shape.promptRows;
    const std::size_t before = model.promptScroll.value_or(shape.promptFirst);
    std::size_t after = std::min(before + 1, last);
    if (direction == EdgeScroll::Earlier) {
        after = before > 0 ? before - 1 : 0;
    }
    if (after == before) {
        return false;
    }
    model.promptScroll = after;
    // The draft moved under a span that stays on the screen, so the span goes the other way to
    // stay over the same words.
    interaction.selection.slide(static_cast<std::ptrdiff_t>(before) -
                                static_cast<std::ptrdiff_t>(after));
    interaction.selection.extend(interaction.pointer.row, interaction.pointer.column);
    return true;
}

/// Starts what the left button does where it went down.
/// @param key Key that arrived, which carries the position.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, for the hints and the scroll.
/// @param scrollStep Rows a page scroll takes, as `update` wants.
void pressAt(const Key& key, Interaction& interaction, Model& model, std::size_t scrollStep) {
    interaction.drag = Interaction::Drag::None;
    // The bar of the hints takes hold first: dragging it walks the list the way dragging the
    // bar of the conversation scrolls it.
    if (overHintBar(interaction, key.column, key.row)) {
        interaction.drag = Interaction::Drag::HintBar;
        dragHintBarTo(interaction, model, key.row);
        return;
    }
    // A hint takes a click the way it takes tab: the one under the pointer is written into the
    // draft, and the selection is dropped rather than started over the panel.
    if (const std::optional<std::size_t> hint = hintAt(interaction, key.column, key.row);
        hint.has_value()) {
        model.hintIndex = *hint;
        (void)Domain::update(model, Key{Key::Kind::Tab, 0}, scrollStep);
        interaction.selection.clear();
        return;
    }
    if (overScrollBar(interaction, key.column, key.row)) {
        grabBar(interaction, model, key.row);
        return;
    }
    if (overPromptBar(interaction, key.column, key.row)) {
        interaction.drag = Interaction::Drag::PromptBar;
        dragPromptBarTo(interaction, model, key.row);
        return;
    }
    // A second press on the same cell takes the word under it and a third takes the row,
    // which is what picking text out means everywhere else. The rows of the viewport are the
    // conversation's, which scrolls; the rest of the screen stays where it is.
    const int count =
        interaction.clicks.press(key.row, key.column, std::chrono::steady_clock::now());
    const Presentation::Scope scope = scopeAt(interaction, key.row);
    if (count == 2) {
        interaction.selection =
            Presentation::wordAt(interaction.shape.frame, key.row, key.column, scope);
        return;
    }
    if (count > 2) {
        interaction.selection = Presentation::lineAt(interaction.shape.frame, key.row, scope);
        return;
    }
    interaction.selection.begin(key.row, key.column, scope);
    // A plain press holds the far end of the selection: it is what a drag stretches, and what
    // keeps the text moving while the pointer waits at an edge of the viewport.
    interaction.drag = Interaction::Drag::Selection;
}

/// Follows the pointer with whatever it is holding: a bar, or the far end of the selection.
/// @param key Key that arrived, which carries the position.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, which holds the scroll and the hints.
void dragTo(const Key& key, Interaction& interaction, Model& model) {
    switch (interaction.drag) {
    case Interaction::Drag::HintBar:
        dragHintBarTo(interaction, model, key.row);
        return;
    case Interaction::Drag::ConversationBar:
        dragBarTo(interaction, model, key.row);
        return;
    case Interaction::Drag::PromptBar:
        dragPromptBarTo(interaction, model, key.row);
        return;
    case Interaction::Drag::Selection:
        // A word or a row taken by a double click stays as it is: the pointer is still inside it.
        if (interaction.clicks.count() < 2) {
            interaction.selection.extend(key.row, key.column);
        }
        return;
    case Interaction::Drag::None:
        return;
    }
}

/// Scrolls with the wheel or the page keys, which move whatever the pointer is over.
/// @param key Key that arrived.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, which holds the hint selection and the panel of the prompt.
/// @return Whether what is under the pointer took it, which leaves the conversation where it is.
bool scrollWith(const Key& key, const Interaction& interaction, Model& model) {
    // A wheel report carries the cell it happened on; the page keys do not, so they go by the
    // last place the pointer was seen, which is the hover the panel is under.
    const bool fromWheel = key.kind == Key::Kind::WheelUp || key.kind == Key::Kind::WheelDown;
    const std::size_t row = fromWheel ? key.row : interaction.pointer.row;
    const std::size_t column = fromWheel ? key.column : interaction.pointer.column;
    if (hintAt(interaction, column, row).has_value()) {
        stepHint(model, key.kind == Key::Kind::WheelDown || key.kind == Key::Kind::PageDown);
        return true;
    }
    // The wheel over the prompt moves the draft when the panel is too short for it; the page
    // keys stay with the conversation, which is what they are for.
    const Presentation::RenderResult& shape = interaction.shape;
    if (fromWheel && shape.promptTotal > shape.promptRows && overPromptBox(interaction, row)) {
        const std::size_t last = shape.promptTotal - shape.promptRows;
        const std::size_t here = model.promptScroll.value_or(shape.promptFirst);
        if (key.kind == Key::Kind::WheelUp) {
            model.promptScroll = here > 0 ? here - 1 : 0;
        } else {
            model.promptScroll = std::min(here + 1, last);
        }
        return true;
    }
    return false;
}

}  // namespace

KeyResult handleKey(Interaction& interaction, Model& model, const Key& key, std::size_t scrollStep,
                    std::string& clipboard, Terminal& terminal) {
    const Presentation::Pointer before = interaction.pointer;
    switch (key.kind) {
    case Key::Kind::MousePress:
    case Key::Kind::MouseDrag:
    case Key::Kind::MouseRelease:
    case Key::Kind::MouseMove:
        movePointer(interaction, key.row, key.column);
        break;
    default:
        break;
    }

    switch (key.kind) {
    case Key::Kind::MousePress:
        pressAt(key, interaction, model, scrollStep);
        return {true, true};
    case Key::Kind::MouseDrag:
        dragTo(key, interaction, model);
        return {true, true};
    case Key::Kind::MouseRelease: {
        const Interaction::Drag dragging = interaction.drag;
        interaction.drag = Interaction::Drag::None;
        if (dragging == Interaction::Drag::HintBar ||
            dragging == Interaction::Drag::ConversationBar) {
            return {true, true};
        }
        dragTo(key, interaction, model);
        return {true, true};
    }
    case Key::Kind::MouseMove: {
        // Only what is under the pointer can have changed, and a frame that would look the same
        // is never written.
        const bool hoverMoved = before.onScreen != interaction.pointer.onScreen ||
                                before.row != interaction.pointer.row ||
                                before.column != interaction.pointer.column;
        return {true, hoverMoved};
    }
    case Key::Kind::WheelUp:
    case Key::Kind::WheelDown:
    case Key::Kind::PageUp:
    case Key::Kind::PageDown:
        return {true, scrollWith(key, interaction, model)};
    case Key::Kind::CtrlC:
        // The copy key, and nothing else. A client that leaves on it is a client someone leaves
        // by accident while reaching for a selection.
        copySelection(interaction, interaction.shape.frame, clipboard, terminal);
        interaction.selection.clear();
        return {true, true};
    case Key::Kind::CtrlV:
        if (clipboard.empty()) {
            return {true, false};  // Nothing to paste, and the model has no use for the key.
        }
        insertText(model, clipboard, scrollStep);
        interaction.selection.clear();
        return {true, true};
    default:
        return {};
    }
}

std::vector<Domain::Effect> applyKey(Interaction& interaction, Model& model, const Key& key,
                                     std::size_t scrollStep) {
    const bool scrolls = scrollsTranscript(key.kind);
    if (!scrolls) {
        interaction.selection.clear();
    }
    const std::size_t offsetBefore = model.scrollOffset;
    std::vector<Domain::Effect> effects = Domain::update(model, key, scrollStep);
    if (scrolls) {
        // The model asks for the rows, and the viewport gives what it has: the picking follows
        // the rows that were really shown, not the ones that were asked for.
        const std::size_t offsetAfter =
            std::min(model.scrollOffset, interaction.shape.maxScrollOffset);
        model.scrollOffset = offsetAfter;
        interaction.selection.shift(static_cast<std::ptrdiff_t>(offsetAfter) -
                                    static_cast<std::ptrdiff_t>(offsetBefore));
    }
    return effects;
}

bool autoscrollAtEdge(Interaction& interaction, Model& model,
                      std::chrono::steady_clock::time_point now) {
    const EdgeDrag drag = edgeDragOf(interaction);
    if (drag.target == EdgeTarget::None || now < interaction.nextAutoScroll) {
        return false;
    }
    interaction.nextAutoScroll = now + AutoScrollStep;
    if (drag.target == EdgeTarget::Prompt) {
        return autoScrollPrompt(interaction, model, drag.direction);
    }
    return autoScrollConversation(interaction, model, drag.direction);
}

}  // namespace OpenAnimaTui::Application
