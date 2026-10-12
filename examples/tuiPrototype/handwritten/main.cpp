/// @file
/// @brief Throwaway prototype: an inline terminal client written by hand.
///
/// It exists to be compared against the FTXUI one and to be thrown away: same problem,
/// same three things (the prompt, the answer growing in place, the key that cancels), so
/// that the effort and the seams of each approach can be seen side by side.
///
/// Unix only on purpose. What Windows would cost is part of what the comparison prices in.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "OpenAnima/OpenAnima.h"

#include "dotenv.h"
#include "terminal.h"

namespace {

using OpenAnima::Core::EventKind;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::NPCBrain;
using OpenAnima::Core::RequestId;
using OpenAnimaPrototype::Key;

constexpr const char* Reset = "\x1b[0m";
constexpr const char* Dim = "\x1b[2m";
constexpr const char* Bold = "\x1b[1m";
constexpr const char* Blue = "\x1b[34m";
constexpr const char* Green = "\x1b[32m";

/// The name the NPC answers under, and the width every prefix is padded to.
constexpr const char* NpcName = "Aldric";
constexpr std::size_t PrefixWidth = 8;

/// Everything the screen needs to know.
struct Screen {
    std::string answer;        ///< Text produced by the turn in flight.
    std::string draft;         ///< What the user is typing.
    std::string model;         ///< Model that answers, for the status line.
    std::string hint;          ///< What the status line says when nothing is running.
    bool busy{false};          ///< A turn is in flight.
    bool cancelled{false};     ///< The turn in flight was cancelled.
    std::size_t liveLines{0};  ///< Lines drawn below the last committed one.
    std::chrono::steady_clock::time_point startedAt;  ///< When the turn started.
};

std::size_t elapsedTenths(const Screen& screen) {
    if (!screen.busy) {
        return 0;
    }
    const auto elapsed = std::chrono::steady_clock::now() - screen.startedAt;
    return static_cast<std::size_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() / 100);
}

std::string prefix(const std::string& speaker, const char* colour) {
    std::string text = speaker;
    text.resize(std::max(PrefixWidth, speaker.size() + 2), ' ');
    return std::string{colour} + text + Reset;
}

/// @return One committed line, wrapped and with the prefix of its speaker.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- speaker first reads better.
std::string renderLine(const std::string& speaker, const std::string& text, std::size_t width) {
    const char* colour = speaker == NpcName ? Green : Blue;
    const std::size_t bodyWidth = width > PrefixWidth ? width - PrefixWidth : 1;
    const std::vector<std::string> wrapped = OpenAnimaPrototype::wrap(text, bodyWidth);

    std::string rendered;
    for (std::size_t index = 0; index < wrapped.size(); ++index) {
        rendered += index == 0 ? prefix(speaker, colour) : std::string(PrefixWidth, ' ');
        rendered += wrapped[index];
        rendered += '\n';
    }
    return rendered;
}

/// Erases the live region, leaving the cursor where the frame started.
void eraseLive(Screen& screen) {
    if (screen.liveLines == 0) {
        return;
    }
    OpenAnimaPrototype::write("\x1b[" + std::to_string(screen.liveLines) + "A\r\x1b[J");
    screen.liveLines = 0;
}

/// Prints a line for good, so it stays in the scrollback.
void printLine(const std::string& speaker, const std::string& text) {
    OpenAnimaPrototype::write(renderLine(speaker, text, OpenAnimaPrototype::terminalWidth()));
}

/// Draws the live region: the answer in flight and the prompt under it.
void draw(Screen& screen) {
    const std::size_t width = OpenAnimaPrototype::terminalWidth();
    const std::size_t tenths = elapsedTenths(screen);

    std::string frame;
    if (screen.busy) {
        frame += renderLine(NpcName, screen.answer.empty() ? "..." : screen.answer, width);
        frame += std::string(PrefixWidth, ' ') + Dim + "working " + std::to_string(tenths / 10) +
                 "." + std::to_string(tenths % 10) + "s   esc to cancel" + Reset + "\n";
    } else {
        frame +=
            std::string(PrefixWidth, ' ') + Dim + screen.model + "   " + screen.hint + Reset + "\n";
    }
    frame += std::string{Bold} + "> " + Reset + screen.draft;

    OpenAnimaPrototype::write("\x1b[J" + frame);
    screen.liveLines = static_cast<std::size_t>(std::count(frame.begin(), frame.end(), '\n'));
}

/// Reads one key and does what it says.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) -- the state, the brain and the key.
void handleKey(Screen& screen, NPCBrain& brain, const Key& key, RequestId& turn, bool& running) {
    switch (key.kind) {
    case Key::Kind::Character:
        screen.draft += key.character;
        return;
    case Key::Kind::Backspace:
        if (!screen.draft.empty()) {
            screen.draft.pop_back();
        }
        return;
    case Key::Kind::Enter:
        if (!screen.busy && !screen.draft.empty()) {
            eraseLive(screen);
            printLine("you", screen.draft);
            turn = brain.askAsync(screen.draft);
            screen.draft.clear();
            screen.busy = true;
            screen.cancelled = false;
            screen.startedAt = std::chrono::steady_clock::now();
        }
        return;
    case Key::Kind::Escape:
        if (screen.busy) {
            brain.cancel(turn);
        }
        return;
    case Key::Kind::CtrlC:
        if (screen.busy) {
            brain.cancel(turn);
        } else if (screen.draft.empty()) {
            running = false;
        } else {
            screen.draft.clear();
        }
        return;
    case Key::Kind::CtrlD:
        running = false;
        return;
    case Key::Kind::None:
    case Key::Kind::Unknown:
        return;
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

    Screen screen;
    screen.model = config.model;
    screen.hint = "ask something, esc cancels a turn, ctrl+d leaves";

    try {
        OpenAnimaPrototype::RawMode rawMode;
        draw(screen);

        bool running = true;
        std::size_t drawnTenths = 0;
        RequestId turn{0};

        while (running) {
            if (const auto key = OpenAnimaPrototype::readKey(std::chrono::milliseconds{20})) {
                handleKey(screen, brain, *key, turn, running);
                draw(screen);
            }

            while (const auto event = brain.poll()) {
                switch (event->kind) {
                case EventKind::Started:
                    break;
                case EventKind::Delta:
                    screen.answer += event->text;
                    draw(screen);
                    break;
                case EventKind::Finished:
                    screen.cancelled = event->completion.finishReason == FinishReason::Cancelled;
                    screen.busy = false;
                    eraseLive(screen);
                    printLine(NpcName, screen.answer);
                    if (screen.cancelled) {
                        OpenAnimaPrototype::write(std::string(PrefixWidth, ' ') + Dim +
                                                  "(cancelled, what it had is kept)" + Reset +
                                                  "\n");
                    }
                    screen.answer.clear();
                    screen.cancelled = false;
                    draw(screen);
                    break;
                case EventKind::Failed:
                    screen.busy = false;
                    eraseLive(screen);
                    printLine(NpcName, "[" + event->error + "]");
                    screen.answer.clear();
                    draw(screen);
                    break;
                }
            }

            // The clock on the status line is the only thing that changes on its own.
            if (screen.busy && elapsedTenths(screen) != drawnTenths) {
                drawnTenths = elapsedTenths(screen);
                eraseLive(screen);
                draw(screen);
            }
        }

        eraseLive(screen);
        OpenAnimaPrototype::write("\n");
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
    return EXIT_SUCCESS;
}
