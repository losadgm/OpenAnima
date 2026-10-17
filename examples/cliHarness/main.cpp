/// @file
/// @brief The command line client: options in, prompts out, turns printed as they land.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "OpenAnima/OpenAnima.h"

#include "cliHarness/options.h"
#include "cliHarness/session.h"
#include "common/apiSettings.h"
#include "common/dotenv.h"

using OpenAnima::Core::Event;
using OpenAnima::Core::EventKind;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::NpcAction;
using OpenAnima::Core::NPCBrain;
using OpenAnima::Core::NpcReply;
using OpenAnima::Core::RequestId;
using OpenAnima::Provider::RestProviderConfig;
using OpenAnimaCli::Options;
using OpenAnimaCli::Order;
using OpenAnimaCli::OrderKind;
using OpenAnimaCli::Session;
using OpenAnimaCli::Step;
using OpenAnimaCli::UsageError;

namespace {

/// The variable prefix this client reads, which is what keeps it apart from the console
/// client running in the same shell.
constexpr const char* Prefix = "OPENANIMA_CLI";

/// Where the ctrl-c presses are counted.
std::atomic<int>& interrupts() {
    static std::atomic<int> count{0};
    return count;
}

/// Counts a ctrl-c press. Nothing else happens here: the turn is cancelled by the loop
/// that polls it, never from the signal.
void countInterrupt([[maybe_unused]] int number) {
    interrupts().fetch_add(1);
}

/// @return Whether stdin is a terminal, which decides whether prompts are printed.
bool stdinIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdin)) != 0;
#else
    return ::isatty(fileno(stdin)) != 0;
#endif
}

/// Reads the settings from the environment, then from the command line, which wins.
/// @param options What the command line said.
/// @return The configuration to build the provider with.
/// @throws UsageError when a required value is missing or out of its range.
RestProviderConfig readConfiguration(const Options& options) {
    RestProviderConfig config;
    OpenAnimaExample::applyApiSettings(config, Prefix);
    OpenAnimaCli::applyOptions(options, config);

    if (config.baseUrl.empty()) {
        throw UsageError("no API root: pass --base-url <url> or set " + std::string{Prefix} +
                         "_BASE_URL");
    }
    if (config.model.empty()) {
        throw UsageError("no model: pass --model <name> or set " + std::string{Prefix} + "_MODEL");
    }
    try {
        config.validate();
    } catch (const OpenAnima::ConfigError& error) {
        throw UsageError(error.what());
    }
    return config;
}

/// Prints what comes after the dialogue: why the turn ended, what the npc thought, what it
/// asks the host to do, and the document as it arrived when the client asked for it.
/// @param event Finished event of the turn.
/// @param raw Whether to print the document.
void printTurnEnd(const Event& event, bool raw) {
    std::cout << '\n';
    if (event.completion.finishReason == FinishReason::Cancelled) {
        std::cout << "(turn stopped, what it said was kept)\n";
    } else if (event.completion.finishReason == FinishReason::Length) {
        std::cout << "(the reply hit the token limit)\n";
    } else if (event.completion.finishReason == FinishReason::ContentFilter) {
        std::cout << "(the provider trimmed the reply)\n";
    }

    if (!event.reply.has_value()) {
        return;
    }

    const NpcReply& reply = *event.reply;
    if (!reply.reasoning.empty()) {
        std::cout << "~ " << reply.reasoning << '\n';
    }
    for (const NpcAction& action : reply.actions) {
        std::cout << "  [" << action.name << ']';
        if (!action.arguments.empty()) {
            std::cout << ' ' << action.arguments.dump();
        }
        std::cout << '\n';
    }
    if (raw) {
        std::cout << "--- raw ---\n";
        if (reply.document.is_null()) {
            std::cout << "(no document: the turn was stopped before it was whole)\n";
        } else {
            std::cout << reply.document.dump(2) << '\n';
        }
    }
    std::cout << std::flush;
}

/// Asks the npc and prints the dialogue while it is written.
/// @param brain Npc to talk to.
/// @param prompt What to send.
/// @param raw Whether to print the document of every turn.
/// @return False when ctrl-c was pressed a second time and the client should leave.
bool runTurn(NPCBrain& brain, const std::string& prompt, bool raw) {
    const int before = interrupts().load();
    const RequestId turn = brain.askTurnAsync(prompt);
    bool cancelling = false;

    for (;;) {
        const std::optional<Event> event = brain.poll(std::chrono::milliseconds{50});
        const int pressed = interrupts().load();
        if (pressed > before) {
            if (pressed - before > 1) {
                std::cout << '\n';
                return false;
            }
            if (!cancelling) {
                brain.cancel(turn);
                cancelling = true;
                std::cout << "\n(cancelling, ctrl-c again to leave)\n" << std::flush;
            }
        }
        if (!event.has_value()) {
            continue;
        }
        if (event->kind == EventKind::Delta) {
            std::cout << event->text << std::flush;
            continue;
        }
        if (event->kind == EventKind::Failed) {
            std::cerr << "error: " << event->error << '\n';
            return true;
        }
        if (event->kind == EventKind::Finished) {
            printTurnEnd(*event, raw);
            return true;
        }
    }
}

/// Builds the brain of one npc: the configuration they share, the persona of this one.
/// @param config Configuration every brain is built with.
/// @param persona What this npc is.
/// @return The brain, ready to be asked.
NPCBrain makeBrain(const RestProviderConfig& config, const std::string& persona) {
    NPCBrain brain = OpenAnima::makeNpc(config);
    brain.setSystemPrompt(persona);
    return brain;
}

/// Carries out one order of the session.
/// @param order What the session asked for.
/// @param brains Brains that exist, by npc id.
/// @param config Configuration every new brain is built with.
/// @param raw Whether the document of every turn is printed.
/// @return False when ctrl-c was pressed twice and the client should leave.
bool carryOut(const Order& order, std::map<std::size_t, NPCBrain>& brains,
              const RestProviderConfig& config, bool raw) {
    if (order.kind == OrderKind::CreateNpc) {
        brains.emplace(order.npcId, makeBrain(config, order.text));
        return true;
    }
    if (order.kind != OrderKind::Clear && order.kind != OrderKind::Ask) {
        return true;
    }

    const auto brain = brains.find(order.npcId);
    if (brain == brains.end()) {
        std::cerr << "open_anima_cli: npc " << order.npcId << " has no model behind\n";
        return true;
    }
    if (order.kind == OrderKind::Clear) {
        brain->second.clearHistory();
        return true;
    }
    return runTurn(brain->second, order.text, raw);
}

}  // namespace

/// Talks to a chat-completions endpoint: one prompt from the command line, or a session of
/// them from stdin, with the npcs the client creates.
/// @return EXIT_SUCCESS when the session ended, 2 when the command line was wrong, 130 when
///         ctrl-c was pressed twice.
int main(int argc, char** argv) {
    Options options;
    try {
        options = OpenAnimaCli::parseArguments(std::vector<std::string>{argv + 1, argv + argc});
    } catch (const UsageError& error) {
        std::cerr << "open_anima_cli: " << error.what() << '\n';
        return 2;
    }

    if (options.help) {
        std::cout << OpenAnimaCli::helpText();
        return EXIT_SUCCESS;
    }
    if (options.version) {
        std::cout << OpenAnimaCli::versionText() << '\n';
        return EXIT_SUCCESS;
    }

    // The CMake Tools buttons pass no environment of their own, so the file is loaded
    // here. Real variables win: the file only fills in what is missing.
    OpenAnimaExample::applyDotEnv(OpenAnimaExample::findDotEnv());

    RestProviderConfig config;
    try {
        config = readConfiguration(options);
    } catch (const UsageError& error) {
        std::cerr << "open_anima_cli: " << error.what() << '\n';
        return 2;
    } catch (const OpenAnima::OpenAnimaError& error) {
        std::cerr << "open_anima_cli: " << error.what() << '\n';
        return 2;
    }

    // Installed before the first turn, so that a session can be stopped half way.
    interrupts().store(0);
    std::signal(SIGINT, countInterrupt);

    const bool interactive = stdinIsTerminal();
    Session session;
    session.setRaw(options.raw);

    // Every npc has its own brain: its own history and its own catalogue.
    std::map<std::size_t, NPCBrain> brains;

    std::optional<std::size_t> firstNpc;
    if (options.persona.has_value()) {
        firstNpc = session.create(*options.persona);
        brains.emplace(*firstNpc, makeBrain(config, *options.persona));
    }

    if (options.prompt.has_value()) {
        if (!firstNpc.has_value()) {
            std::cerr << "open_anima_cli: no npc to answer: add --persona <text> to create one\n";
            return 2;
        }
        return runTurn(brains.at(*firstNpc), *options.prompt, session.raw()) ? EXIT_SUCCESS : 130;
    }

    if (interactive) {
        std::cout << "talking to " << config.model << " at " << config.baseUrl << '\n';
        if (!firstNpc.has_value()) {
            std::cout << "no npc yet   /create <description> to make one\n";
        }
        std::cout << "/help for the commands, ctrl-d to leave\n" << std::flush;
    }

    std::string line;
    for (;;) {
        if (interactive) {
            std::cout << session.prompt() << std::flush;
        }
        if (!std::getline(std::cin, line)) {
            break;
        }

        const Step step = session.interpret(line);
        for (const std::string& note : step.notes) {
            std::cout << note << '\n';
        }
        if (step.quit) {
            break;
        }
        if (!carryOut(step.order, brains, config, session.raw())) {
            return 130;
        }
        std::cout << std::flush;
    }
    return EXIT_SUCCESS;
}
