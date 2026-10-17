/// @file
/// @brief Everything the library exposes, in one include, plus the entry point.

#pragma once

#include <memory>

// This header is the umbrella: it re-exports the pieces so that one include is enough.
// The marked includes are intentional even where nothing in this file names the type.
#include "OpenAnima/Core/ActionCatalogue.h"  // IWYU pragma: keep
#include "OpenAnima/Core/Completion.h"       // IWYU pragma: keep
#include "OpenAnima/Core/NPCBrain.h"
#include "OpenAnima/Core/NpcReply.h"           // IWYU pragma: keep
#include "OpenAnima/Core/ResponseFormat.h"     // IWYU pragma: keep
#include "OpenAnima/Core/Types.h"              // IWYU pragma: keep
#include "OpenAnima/Errors.h"                  // IWYU pragma: keep
#include "OpenAnima/Http/CurlHttpTransport.h"  // IWYU pragma: keep
#include "OpenAnima/Http/HttpTransport.h"
#include "OpenAnima/Provider/IProvider.h"     // IWYU pragma: keep
#include "OpenAnima/Provider/RestProvider.h"  // IWYU pragma: keep
#include "OpenAnima/Provider/RestProviderConfig.h"

namespace OpenAnima {

/// Builds an NPC with an explicit configuration on the default transport.
/// @param config Endpoint, model and generation settings.
/// @return An NPC ready to ask.
/// @throws ConfigError when a numeric setting falls outside its accepted range.
Core::NPCBrain makeNpc(Provider::RestProviderConfig config);

/// Builds an NPC on an explicit transport.
/// This is the escape hatch: tests, local backends and any custom HTTP boundary go
/// through here instead of the default stack.
/// @param config Endpoint, model and generation settings.
/// @param transport HTTP boundary used for every call.
/// @return An NPC ready to ask.
Core::NPCBrain makeNpc(Provider::RestProviderConfig config,
                       std::shared_ptr<Http::HttpTransport> transport);

}  // namespace OpenAnima
