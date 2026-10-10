#include <iostream>

#include "OpenNPC/NPCBrain.h"

/// Minimal harness: instantiates the brain and prints its status.
/// @return EXIT_SUCCESS on a successful round-trip.
int main() {
    OpenNPC::NPCBrain brain;
    std::cout << brain.ping() << '\n';
    return 0;
}
