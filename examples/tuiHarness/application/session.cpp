#include "session.h"

#include <algorithm>
#include <chrono>
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

Session::Session(const OpenAnima::Provider::RestProviderConfig& config) : m_brains{config} {
    m_model.modelName = config.model;
}

bool Session::step(Terminal& terminal) {
    // What the terminal has not taken yet goes first: while something is still on its way there
    // is no point in deciding what the screen looks like next, and saying it twice would only
    // queue another frame behind the one that has not arrived.
    const bool drained = terminal.flush();

    if (const auto key = terminal.readKey(std::chrono::milliseconds{20})) {
        const std::size_t scrollStep = terminal.height() / 2;
        const KeyResult result =
            handleKey(m_shell, m_model, *key, scrollStep, m_clipboard, terminal);
        if (result.used) {
            m_dirty = m_dirty || result.painted;
        } else {
            m_brains.run(applyKey(m_shell, m_model, *key, scrollStep));
            m_dirty = true;
        }
    }

    // A drag that runs into an edge of the viewport keeps the text moving while the pointer
    // waits there, so a span can reach past what is on screen.
    if (autoscrollAtEdge(m_shell, m_model, std::chrono::steady_clock::now())) {
        m_dirty = true;
    }

    if (m_brains.drain(m_model)) {
        m_dirty = true;
    }

    // The clock on the working line is the only thing that changes on its own.
    if (m_model.busy && Domain::elapsedTenths(m_model) != m_drawnTenths) {
        m_dirty = true;
    }

    if (m_dirty && drained) {
        const Presentation::RenderResult rendered = Presentation::render(
            m_model, terminal.width(), terminal.height(), m_shell.selection, m_shell.pointer);
        // The viewport cannot hide more rows than there are. Holding the model to the real
        // range is what makes the wheel answer at once on the way back down.
        m_model.scrollOffset = std::min(m_model.scrollOffset, rendered.maxScrollOffset);
        m_writer.setCaret(rendered.caretRow, rendered.caretColumn);
        m_frame.clear();
        m_writer.emit(rendered.frame, m_frame);
        terminal.write(m_frame);
        m_shell.shape = rendered;
        m_drawnTenths = Domain::elapsedTenths(m_model);
        m_dirty = false;
    }

    return m_model.running;
}

const Domain::Model& Session::model() const {
    return m_model;
}

}  // namespace OpenAnimaTui::Application
