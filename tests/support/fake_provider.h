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
using OpenAnima::Provider::DeltaSink;
using OpenAnima::Provider::IProvider;

/// In-memory backend: records the conversations it received and replays a scripted
/// answer, so NPCBrain can be tested without a network or a model.
class FakeProvider : public IProvider {
public:
    /// The replayed answer starts with a text, so a test only sets what it asserts.
    FakeProvider() {
        answer.text = "understood";
    }

    Completion complete(const std::vector<ChatMessage>& messages,
                        const DeltaSink& onDelta) override {
        requests.push_back(messages);
        if (failure != nullptr) {
            throw OpenAnimaError(failure);
        }
        for (const std::string& piece : deltas) {
            if (onDelta) {
                onDelta(piece);
            }
        }
        return answer;
    }

    std::vector<std::vector<ChatMessage>> requests;  ///< Every conversation received, in order.
    Completion answer;                               ///< Answer replayed for every call.
    std::vector<std::string> deltas;  ///< Pieces reported to the sink before answering.
    const char* failure{nullptr};     ///< When set, complete() throws.
};

}  // namespace OpenAnima::TestSupport
