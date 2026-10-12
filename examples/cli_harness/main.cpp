#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

// One include: the umbrella re-exports the whole public API.
#include "OpenAnima/OpenAnima.h"

#include "dotenv.h"

using OpenAnima::Core::NPCBrain;
using OpenAnima::Provider::RestProviderConfig;

namespace {

/// Joins the command line into a single prompt.
std::string joinArguments(int argc, char** argv) {
    std::string prompt;
    for (int index = 1; index < argc; ++index) {
        if (index > 1) {
            prompt += ' ';
        }
        prompt += argv[index];
    }
    return prompt;
}

/// Sends a prompt and prints the answer while the provider is still producing it.
/// @param brain Npc to talk to.
/// @param prompt Text to send.
void printAnswer(NPCBrain& brain, const std::string& prompt) {
    brain.ask(prompt, [](std::string_view piece) { std::cout << piece << std::flush; });
    std::cout << '\n';
}

}  // namespace

/// Minimal harness: sends prompts to a chat-completions endpoint and prints the replies.
/// @return EXIT_SUCCESS on a completed session, 1 when a request failed, 2 when the
/// environment is not configured.
int main(int argc, char** argv) {
    // The CMake Tools run and debug buttons pass no environment of their own, so the
    // file is loaded here. Real variables win: the file only fills in what is missing.
    const std::string dotEnvPath = OpenAnimaExample::findDotEnv();
    OpenAnimaExample::applyDotEnv(dotEnvPath);
    if (!dotEnvPath.empty()) {
        std::cerr << "note: loaded " << dotEnvPath << '\n';
    }

    RestProviderConfig config;
    try {
        config = RestProviderConfig::fromEnvironment();
    } catch (const OpenAnima::ConfigError& error) {
        std::cerr << "configuration error: " << error.what() << "\n\n"
                  << "usage: open_anima_cli [prompt words...]\n"
                  << "\n"
                  << "With arguments, sends them as one prompt and exits.\n"
                  << "Without arguments, reads prompts from stdin until end of input.\n"
                  << "\n"
                  << "Required variables:\n"
                  << "  OPENANIMA_BASE_URL           API root, for example "
                     "http://localhost:11434/v1\n"
                  << "  OPENANIMA_MODEL              model name, for example qwen2.5:7b\n"
                  << "Optional variables:\n"
                  << "  OPENANIMA_API_KEY            bearer token, empty for local servers\n"
                  << "  OPENANIMA_TIMEOUT_SECONDS    per-request budget, default 60\n"
                  << "  OPENANIMA_TEMPERATURE        sampling temperature, default 0.7\n"
                  << "  OPENANIMA_MAX_TOKENS         reply length bound, default 512\n"
                  << "\n"
                  << "A .env file in the working directory or in any parent directory is\n"
                  << "loaded before the environment is read.\n";
        return 2;
    }

    NPCBrain brain = OpenAnima::makeNpc(config);

    // An empty command line opens a session instead of sending an empty prompt.
    const std::string prompt = joinArguments(argc, argv);
    if (!prompt.empty()) {
        try {
            printAnswer(brain, prompt);
        } catch (const OpenAnima::OpenAnimaError& error) {
            std::cerr << "error: " << error.what() << '\n';
            return 1;
        }
        return EXIT_SUCCESS;
    }

    std::cout << "talking to " << config.model << " at " << config.baseUrl
              << ", end of input or 'quit' to leave\n";
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line) || line == "quit" || line == "exit") {
            break;
        }
        if (line.empty()) {
            continue;
        }
        try {
            printAnswer(brain, line);
        } catch (const OpenAnima::OpenAnimaError& error) {
            std::cerr << "error: " << error.what() << '\n';
        }
    }
    return EXIT_SUCCESS;
}
