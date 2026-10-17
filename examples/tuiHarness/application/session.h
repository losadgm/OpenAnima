/// @file
/// @brief One run of the client: what it knows, what the pointer did, and the loop between them.

#pragma once

#include <cstddef>
#include <string>

#include "OpenAnima/OpenAnima.h"

#include "application/brains.h"
#include "application/interaction.h"
#include "domain/model.h"
#include "presentation/diff.h"
#include "presentation/render.h"
#include "terminal/terminal.h"

namespace OpenAnimaTui::Application {

/// One run of the client.
///
/// It owns the model, the state of the pointer, the brains and the frame writer, and it runs
/// one turn of the loop at a time: what the terminal sent, what the brains answered, and the
/// frame that comes out of it. The entry point is only the part that opens and closes the
/// terminal around it.
class Session {
public:
    /// @param config The backend the brains are built from.
    explicit Session(const OpenAnima::Provider::RestProviderConfig& config);

    /// Runs one turn of the loop: reads what is there, folds the answers in, paints a frame when
    /// something changed.
    /// @param terminal The terminal to read, write and ask the size of.
    /// @return Whether the client goes on running.
    [[nodiscard]] bool step(Terminal& terminal);

    /// @return The model, for the parts that want to look at what the client knows.
    [[nodiscard]] const Domain::Model& model() const;

private:
    Domain::Model m_model;               ///< Everything the client knows.
    Interaction m_shell;                 ///< What the pointer has done to the screen.
    Brains m_brains;                     ///< The brains the conversation runs on.
    Presentation::FrameWriter m_writer;  ///< What is already on the terminal.
    std::string m_clipboard;             ///< What was copied last, kept for the next paste.
    std::string m_frame;                 ///< The bytes of the last frame written.
    bool m_dirty{true};                  ///< Whether the screen has to be painted again.
    std::size_t m_drawnTenths{0};        ///< The tenth of a second the working line was drawn at.
};

}  // namespace OpenAnimaTui::Application
