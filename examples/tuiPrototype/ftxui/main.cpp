/// @file
/// @brief Throwaway prototype: an inline terminal client built on FTXUI.
///
/// Same three things as the hand-written one (the prompt, the answer growing in place,
/// the key that cancels), so that the two can be compared. This is the version that
/// spends a dependency on the task.

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "OpenAnima/OpenAnima.h"

#include "dotenv.h"

namespace {

using OpenAnima::Core::EventKind;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::NPCBrain;
using OpenAnima::Core::RequestId;

constexpr const char* NpcName = "Aldric";
constexpr std::size_t PrefixWidth = 8;

/// What the two threads share: the monitor writes it, the renderer reads it.
struct State {
    std::mutex mutex;
    std::string answer;  ///< Text of the turn in flight.
    std::string note;    ///< Line under the answer when a turn ends.
    std::string model;   ///< Model that answers.
    std::string hint;    ///< What the quiet line says when nothing runs.
    bool busy{false};
    std::size_t tenths{0};  ///< How long the turn has been running.
    std::chrono::steady_clock::time_point startedAt;
    RequestId turn{0};
};

/// Pumps the brain and wakes the interface up when something moved.
void monitor(NPCBrain& brain, State& state, ftxui::ScreenInteractive& screen,
             std::atomic<bool>& running) {
    while (running) {
        bool changed = false;
        try {
            while (const auto event = brain.poll()) {
                std::lock_guard<std::mutex> lock{state.mutex};
                switch (event->kind) {
                case EventKind::Started:
                    break;
                case EventKind::Delta:
                    state.answer += event->text;
                    changed = true;
                    break;
                case EventKind::Finished:
                    state.busy = false;
                    state.answer = event->completion.text;
                    state.note =
                        event->completion.finishReason == FinishReason::Cancelled
                            ? std::string{PrefixWidth, ' '} + "(cancelled, what it had is kept)"
                            : std::string{};
                    changed = true;
                    break;
                case EventKind::Failed:
                    state.busy = false;
                    state.answer.clear();
                    state.note = std::string{PrefixWidth, ' '} + "[" + event->error + "]";
                    changed = true;
                    break;
                }
            }
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock{state.mutex};
            state.busy = false;
            state.note = std::string{PrefixWidth, ' '} + "[poll failed: " + error.what() + "]";
            changed = true;
        }

        {
            std::lock_guard<std::mutex> lock{state.mutex};
            if (state.busy) {
                const auto elapsed = std::chrono::steady_clock::now() - state.startedAt;
                const std::size_t tenths = static_cast<std::size_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 100);
                if (tenths != state.tenths) {
                    state.tenths = tenths;
                    changed = true;
                }
            }
        }

        if (changed) {
            screen.PostEvent(ftxui::Event::Custom);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
}

}  // namespace

/// Runs the prototype.
/// @return EXIT_SUCCESS, or 2 when the environment is not configured.
int main() {
    const std::string dotEnvPath = OpenAnimaExample::findDotEnv();
    OpenAnimaExample::applyDotEnv(dotEnvPath);

    OpenAnima::Provider::RestProviderConfig config;
    try {
        config = OpenAnima::Provider::RestProviderConfig::fromEnvironment();
    } catch (const OpenAnima::OpenAnimaError& error) {
        std::cerr << "configuration error: " << error.what() << '\n';
        return 2;
    }

    NPCBrain brain = OpenAnima::makeNpc(config);
    brain.setSystemPrompt("You are Aldric, a blacksmith in a small village. Answer in character.");

    State state;
    state.model = config.model;
    state.hint = "ask something, esc cancels a turn, ctrl+d leaves";

    std::string draft;

    // Sending is one call into the library: the worker picks the turn up from there.
    auto send = [&] {
        bool accepted = false;
        {
            std::lock_guard<std::mutex> lock{state.mutex};
            accepted = !state.busy && !draft.empty();
        }
        if (!accepted) {
            return;
        }

        std::string prompt = draft;
        draft.clear();
        try {
            const RequestId turn = brain.askAsync(prompt);
            std::lock_guard<std::mutex> lock{state.mutex};
            state.turn = turn;
            state.answer.clear();
            state.note.clear();
            state.busy = true;
            state.tenths = 0;
            state.startedAt = std::chrono::steady_clock::now();
        } catch (const std::exception& error) {
            std::lock_guard<std::mutex> lock{state.mutex};
            state.note = std::string{PrefixWidth, ' '} + "[ask failed: " + error.what() + "]";
        }
    };

    ftxui::InputOption inputOption;
    inputOption.content = &draft;
    inputOption.placeholder = "type here";
    inputOption.multiline = false;
    inputOption.on_enter = send;
    auto input = ftxui::Input(inputOption);

    auto container = ftxui::Container::Vertical({input});

    auto renderer = ftxui::Renderer(container, [&] {
        std::lock_guard<std::mutex> lock{state.mutex};

        ftxui::Elements rows;
        if (state.busy || !state.answer.empty()) {
            rows.push_back(ftxui::hbox({
                ftxui::text(std::string{NpcName} + "  ") | ftxui::color(ftxui::Color::Green),
                ftxui::paragraph(state.answer.empty() ? "..." : state.answer) | ftxui::flex,
            }));
        }
        if (state.busy) {
            rows.push_back(ftxui::text(std::string(PrefixWidth, ' ') + "working " +
                                       std::to_string(state.tenths / 10) + "." +
                                       std::to_string(state.tenths % 10) + "s   esc to cancel") |
                           ftxui::dim);
        } else if (!state.note.empty()) {
            rows.push_back(ftxui::text(state.note) | ftxui::dim);
        } else {
            rows.push_back(
                ftxui::text(std::string(PrefixWidth, ' ') + state.model + "   " + state.hint) |
                ftxui::dim);
        }
        rows.push_back(
            ftxui::hbox({ftxui::text("> ") | ftxui::bold, input->Render() | ftxui::flex}));
        return ftxui::vbox(std::move(rows)) | ftxui::xflex;
    });

    auto screen = ftxui::ScreenInteractive::FitComponent();
    screen.ForceHandleCtrlC(false);

    // The keys the interface keeps for itself. Return is not here: the input takes it.
    auto root = ftxui::CatchEvent(renderer, [&](const ftxui::Event& event) {
        if (event == ftxui::Event::Escape || event == ftxui::Event::CtrlC) {
            std::lock_guard<std::mutex> lock{state.mutex};
            if (state.busy) {
                brain.cancel(state.turn);
                return true;
            }
            return false;
        }
        if (event == ftxui::Event::CtrlD) {
            screen.Exit();
            return true;
        }
        return false;
    });

    std::atomic<bool> monitorRunning{true};
    std::thread monitorThread{monitor, std::ref(brain), std::ref(state), std::ref(screen),
                              std::ref(monitorRunning)};

    screen.Loop(root);

    monitorRunning = false;
    monitorThread.join();

    std::cout << '\n';
    return EXIT_SUCCESS;
}
