/// @file
/// @brief What the pointer does to the screen: what it picks out, what it drags, what it scrolls.
///
/// The model knows nothing about positions, and the renderer knows nothing about time or keys.
/// Everything in between is here: a press becomes a span or a drag, a wheel notch becomes a
/// scroll of whatever is under the pointer, and a drag that waits at an edge keeps the text
/// moving. The shell that owns the loop keeps the brains and the terminal.

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "app/model.h"
#include "screen/render.h"
#include "screen/selection.h"
#include "terminal/terminal.h"

namespace OpenAnimaTui::Shell {

using App::Model;

/// What the pointer has done to the screen: what is picked out, where it is, and what it is
/// dragging. All of it is the shell's business, so the model never hears about a position.
struct Interaction {
    Screen::Selection selection;   ///< The cells the mouse picked out.
    Screen::Clicks clicks;         ///< Presses counted, which a double click is.
    Screen::Pointer pointer;       ///< Where the pointer is.
    Screen::RenderResult shape;    ///< The frame on screen and where its parts sit.
    std::size_t barGrab{0};        ///< Row of the thumb the drag holds.
    bool pullingBar{false};        ///< Whether the bar of the conversation is dragged.
    bool pullingHintBar{false};    ///< Whether the bar of the hints is dragged.
    bool pullingPromptBar{false};  ///< Whether the bar of the prompt is dragged.
    bool pullingSelection{false};  ///< Whether the drag stretches a selection.
    std::chrono::steady_clock::time_point nextAutoScroll;  ///< When the edge may move it on.
};

/// What handling a key did here.
struct KeyResult {
    bool used{false};     ///< The key was about the screen, so the model does not see it.
    bool painted{false};  ///< The screen has to be repainted.
};

/// Handles the keys that are about the screen rather than about the conversation: what the
/// pointer lights up, what it picks out, what it drags, and what is copied and pasted.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, for the hints, the paste and the step a scroll takes.
/// @param key Key that arrived.
/// @param scrollStep Rows a page scroll takes, as `update` wants.
/// @param clipboard What was copied last.
/// @param terminal Terminal the clipboard sequence goes to.
/// @return Whether the key was used here, and whether that changed what is on screen.
[[nodiscard]] KeyResult handleKey(Interaction& interaction, Model& model, const Key& key,
                                  std::size_t scrollStep, std::string& clipboard,
                                  Terminal& terminal);

/// Runs a key through the model, moving what is picked out the way the key moves the text.
///
/// Anything that is not a scroll moves the reader on, and what was picked out is dropped rather
/// than left over text that has changed under it. The wheel and the page keys are the exception:
/// they move the conversation, and what was picked out goes with the words it covers.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, changed in place.
/// @param key Key that arrived.
/// @param scrollStep Rows a page scroll takes, as `update` wants.
/// @return What the shell has to carry out, in order.
[[nodiscard]] std::vector<App::Effect> applyKey(Interaction& interaction, Model& model,
                                                const Key& key, std::size_t scrollStep);

/// Moves the conversation or the panel of the prompt on when a drag waits at an edge.
/// @param interaction What the pointer has done to the screen.
/// @param model The model, changed in place, since the edge holds the offset.
/// @param now The moment the loop is at, which decides when another row is due.
/// @return Whether the screen has to be repainted.
[[nodiscard]] bool autoscrollAtEdge(Interaction& interaction, Model& model,
                                    std::chrono::steady_clock::time_point now);

}  // namespace OpenAnimaTui::Shell
