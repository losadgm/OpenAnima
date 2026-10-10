#include <catch2/catch_test_macros.hpp>

#include "OpenAnima/NPCBrain.h"

TEST_CASE("NPCBrain::ping reports a successful initialization", "[npcbrain]") {
    OpenAnima::NPCBrain brain;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while, bugprone-chained-comparison)
    REQUIRE(brain.ping() == "OpenAnima inicializado correctamente.");
}
