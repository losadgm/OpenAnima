#include "OpenAnima/NPCBrain.h"

namespace OpenAnima {

// Part of the public instance API; kept non-static on purpose.
std::string NPCBrain::ping() {  // NOLINT(readability-convert-member-functions-to-static)
    return "OpenAnima inicializado correctamente.";
}

}  // namespace OpenAnima
