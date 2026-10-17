/// @file
/// @brief The command vocabulary and what completes it.

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <utility>
#include <vector>

#include "domain/commands.h"

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("commandMatches finds what completes the typed prefix") {
    CHECK(OpenAnimaTui::Domain::commandMatches("/", 1).size() ==
          OpenAnimaTui::Domain::commands().size());
    CHECK(OpenAnimaTui::Domain::commandMatches("/c", 2) ==
          std::vector<std::string>{"/create", "/clear"});
    CHECK(OpenAnimaTui::Domain::commandMatches("/h", 2) == std::vector<std::string>{"/help"});
    CHECK(OpenAnimaTui::Domain::commandMatches("/e", 2) == std::vector<std::string>{"/enter"});
    CHECK(OpenAnimaTui::Domain::commandMatches("/r", 2) == std::vector<std::string>{"/raw"});
    CHECK(OpenAnimaTui::Domain::commandMatches("/create a", 8).empty());
    CHECK(OpenAnimaTui::Domain::commandMatches("hello", 5).empty());
}

TEST_CASE("completeCommand replaces the typed prefix and leaves room for the argument") {
    const std::pair<std::string, std::size_t> completed =
        OpenAnimaTui::Domain::completeCommand("/c", 2, "/create");
    CHECK(completed.first == "/create ");
    CHECK(completed.second == 8);

    const std::pair<std::string, std::size_t> withTail =
        OpenAnimaTui::Domain::completeCommand("/c smith", 2, "/create");
    CHECK(withTail.first == "/create  smith");
    CHECK(withTail.second == 8);
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
