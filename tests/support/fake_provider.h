#pragma once

#include <string>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/IProvider.h"

namespace OpenAnima::TestSupport {

using OpenAnima::Core::ChatMessage;
using OpenAnima::Core::Completion;
using OpenAnima::Provider::IProvider;

/// In-memory backend: records the conversations it received and replays a scripted
/// answer, so NPCBrain can be tested without a network or a model.
class FakeProvider : public IProvider {
public:
    /// The replayed answer starts with a text, so a test only sets what it asserts.
    FakeProvider() {
        answer.text = "understood";
    }

    Completion complete(const std::vector<ChatMessage>& messages) override {
        requests.push_back(messages);
        if (failure != nullptr) {
            throw OpenAnimaError(failure);
        }
        return answer;
    }

    std::vector<std::vector<ChatMessage>> requests;  ///< Every conversation received, in order.
    Completion answer;                               ///< Answer replayed for every call.
    const char* failure{nullptr};                    ///< When set, complete() throws.
};

}  // namespace OpenAnima::TestSupport
