#pragma once

#include <functional>
#include <string>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/IProvider.h"

namespace OpenAnima::TestSupport {

using OpenAnima::Core::ChatMessage;
using OpenAnima::Core::Completion;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::ResponseFormat;
using OpenAnima::Provider::Call;
using OpenAnima::Provider::IProvider;

/// In-memory backend: records the conversations it received and replays a scripted
/// answer, so NPCBrain can be tested without a network or a model.
class FakeProvider : public IProvider {
public:
    /// The replayed answer starts with a text, so a test only sets what it asserts.
    FakeProvider() {
        answer.text = "understood";
    }

    Completion complete(const std::vector<ChatMessage>& messages, const Call& call) override {
        requests.push_back(messages);
        formats.push_back(call.format);
        if (failure != nullptr) {
            throw OpenAnimaError(failure);
        }

        std::string produced;
        for (const std::string& piece : deltas) {
            if (beforeEachDelta) {
                beforeEachDelta();
            }
            if (call.keepGoing && !call.keepGoing()) {
                // What a real provider does when the exchange was abandoned: it hands back
                // what it had produced by then, marked for what it is.
                Completion cut = answer;
                cut.text = produced;
                cut.finishReason = FinishReason::Cancelled;
                return cut;
            }
            produced += piece;
            if (call.onDelta) {
                call.onDelta(piece);
            }
        }

        Completion done = answer;
        if (call.format.kind != ResponseFormat::Kind::Text) {
            // What the shipped provider does once the answer is whole: read it as JSON.
            try {
                done.json = nlohmann::ordered_json::parse(done.text);
            } catch (const nlohmann::json::exception&) {
                throw ParseError("the answer was asked for as JSON and is not: " + done.text);
            }
        }
        return done;
    }

    /// Keeps the convenience overloads of the interface reachable through this type.
    using IProvider::complete;

    std::vector<std::vector<ChatMessage>> requests;  ///< Every conversation received, in order.
    std::vector<ResponseFormat> formats;             ///< Shape asked for by every call, in order.
    Completion answer;                               ///< Answer replayed for every call.
    std::vector<std::string> deltas;        ///< Pieces reported to the sink before answering.
    const char* failure{nullptr};           ///< When set, complete() throws.
    std::function<void()> beforeEachDelta;  ///< Runs before each piece, to hold the answer.
};

}  // namespace OpenAnima::TestSupport
