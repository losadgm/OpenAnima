/// @file
/// @brief The command vocabulary and what completes it.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

#include "app/commands.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("commandMatches finds what completes the typed prefix") {
    CHECK(OpenAnimaTui::App::commandMatches("/", 1).size() == OpenAnimaTui::App::commands().size());
    CHECK(OpenAnimaTui::App::commandMatches("/c", 2) ==
          std::vector<std::string>{"/create", "/clear"});
    CHECK(OpenAnimaTui::App::commandMatches("/h", 2) == std::vector<std::string>{"/help"});
    CHECK(OpenAnimaTui::App::commandMatches("/e", 2) == std::vector<std::string>{"/enter"});
    CHECK(OpenAnimaTui::App::commandMatches("/create a", 8).empty());
    CHECK(OpenAnimaTui::App::commandMatches("hello", 5).empty());
}

TEST_CASE("completeCommand replaces the typed prefix and leaves room for the argument") {
    const std::pair<std::string, std::size_t> completed =
        OpenAnimaTui::App::completeCommand("/c", 2, "/create");
    CHECK(completed.first == "/create ");
    CHECK(completed.second == 8);

    const std::pair<std::string, std::size_t> withTail =
        OpenAnimaTui::App::completeCommand("/c smith", 2, "/create");
    CHECK(withTail.first == "/create  smith");
    CHECK(withTail.second == 8);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
