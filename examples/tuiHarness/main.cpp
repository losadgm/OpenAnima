/// @file
/// @brief The entry point of the terminal client: put the layers together and run them.
///
/// Everything the client does lives below: `domain` holds the rules, `presentation` decides what
/// a frame looks like, `application` runs the loop between the two, and `terminal` is the
/// console. What is left here is the order they are built in, which is the only place that has
/// to know all of them.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/RestProviderConfig.h"

#include "application/session.h"
#include "common/apiSettings.h"
#include "common/dotenv.h"
#include "terminal/terminal.h"

/// Runs the client.
/// @return EXIT_SUCCESS, or 2 when the environment is not configured.
int main() {
    const std::string dotEnvPath = OpenAnimaExample::findDotEnv();
    OpenAnimaExample::applyDotEnv(dotEnvPath);

    OpenAnima::Provider::RestProviderConfig config;
    try {
        config = OpenAnimaExample::readApiSettings("OPENANIMA_TUI");
    } catch (const OpenAnima::OpenAnimaError& error) {
        std::cerr << "configuration error: " << error.what() << '\n';
        return 2;
    }

    std::unique_ptr<OpenAnimaTui::Terminal> terminal = OpenAnimaTui::makeTerminal();
    terminal->enterAlternateScreen();

    OpenAnimaTui::Application::Session session{config};
    while (session.step(*terminal)) {
    }

    terminal->flush();
    terminal->leaveAlternateScreen();
    terminal->flush();
    return EXIT_SUCCESS;
}
