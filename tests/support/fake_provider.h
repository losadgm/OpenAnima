#pragma once

#include <string>
#include <vector>

#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/IProvider.h"

namespace OpenAnima::TestSupport {

using OpenAnima::Core::ChatMessage;
using OpenAnima::Provider::IProvider;

/// In-memory backend: records the conversations it received and replays a scripted
/// reply, so NPCBrain can be tested without a network or a model.
class FakeProvider : public IProvider {
public:
    std::string complete(const std::vector<ChatMessage>& messages) override {
        requests.push_back(messages);
        if (failure != nullptr) {
            throw OpenAnimaError(failure);
        }
        return reply;
    }

    std::vector<std::vector<ChatMessage>> requests;  ///< Every conversation received, in order.
    std::string reply{"understood"};                 ///< Reply replayed for every call.
    const char* failure{nullptr};                    ///< When set, complete() throws.
};

}  // namespace OpenAnima::TestSupport
