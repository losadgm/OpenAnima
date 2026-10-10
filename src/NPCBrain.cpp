#include "OpenNPC/NPCBrain.h"

namespace OpenNPC {

// Part of the public instance API; kept non-static on purpose.
std::string NPCBrain::ping() {  // NOLINT(readability-convert-member-functions-to-static)
    return "OpenNPC inicializado correctamente.";
}

}  // namespace OpenNPC
