#include "OpenAnima/Core/NPCBrain.h"

#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/IProvider.h"

namespace OpenAnima::Core {

namespace {

constexpr const char* PingMessage = "OpenAnima core is linked and operational.";

}  // namespace

NPCBrain::NPCBrain(std::shared_ptr<OpenAnima::Provider::IProvider> provider)
    : m_provider(std::move(provider)) {
    assert(m_provider != nullptr && "NPCBrain requires a provider");
}

void NPCBrain::setProvider(std::shared_ptr<OpenAnima::Provider::IProvider> provider) {
    assert(provider != nullptr && "setProvider requires a provider");
    m_provider = std::move(provider);
}

void NPCBrain::setSystemPrompt(std::string prompt) {
    m_systemPrompt = std::move(prompt);
}

Completion NPCBrain::ask(const std::string& prompt) {
    return ask(prompt, Provider::DeltaSink{});
}

Completion NPCBrain::ask(const std::string& prompt, const Provider::DeltaSink& onDelta) {
    assert(!prompt.empty() && "ask() requires a non-empty prompt");

    if (m_provider == nullptr) {
        throw OpenAnimaError("no backend is attached to this brain: call setProvider() first");
    }

    std::vector<ChatMessage> request;
    request.reserve(m_history.size() + 2);
    if (!m_systemPrompt.empty()) {
        request.push_back(ChatMessage{Role::System, m_systemPrompt});
    }
    request.insert(request.end(), m_history.begin(), m_history.end());
    request.push_back(ChatMessage{Role::User, prompt});

    // The answer is fetched before the history is modified, so a failed call leaves the
    // conversation exactly as it was and the client can retry the same turn.
    const Completion answer = m_provider->complete(request, onDelta);

    m_history.push_back(ChatMessage{Role::User, prompt});
    m_history.push_back(ChatMessage{Role::Assistant, answer.text});
    return answer;
}

void NPCBrain::clearHistory() {
    m_history.clear();
}

// Part of the public instance API; kept non-static on purpose.
std::string NPCBrain::ping() {  // NOLINT(readability-convert-member-functions-to-static)
    return PingMessage;
}

}  // namespace OpenAnima::Core
