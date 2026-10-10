#include <catch2/catch_test_macros.hpp>

#include "OpenNPC/NPCBrain.h"

TEST_CASE("NPCBrain::ping reports a successful initialization", "[npcbrain]") {
    OpenNPC::NPCBrain brain;
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-do-while, bugprone-chained-comparison)
    REQUIRE(brain.ping() == "OpenNPC inicializado correctamente.");
}
