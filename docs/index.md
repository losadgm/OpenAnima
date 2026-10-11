# OpenAnima

C++17 middleware that keeps the conversation state of a language-model driven agent and
talks to whichever inference backend you point it at.

The library is a thin layer on purpose. It owns the history of a conversation, builds the
request, and hands back a typed answer with the model that produced it, why generation
stopped, and what the call cost in tokens. Everything else (which provider, which transport,
how the text is used) is decided by the application.

- [Development guide](development.html) - build it, test it, and how these pages are built.
- [API reference](api/index.html) - the classes, in Doxygen form, generated from the headers.

## What it does today

- A conversation object, `OpenAnima::Core::NPCBrain`, with a system prompt, a history, and a
  single call that sends a prompt and returns the answer.
- One provider interface, `OpenAnima::Provider::IProvider`, and a REST implementation for the
  OpenAI chat-completions contract. Ollama, llama.cpp and vLLM speak it too.
- One HTTP boundary, `OpenAnima::Http::HttpTransport`, so providers can be tested without a
  network and the socket code lives in exactly one place.
- Configuration read from the environment, validated, with the ranges checked.

Asynchronous calls, streaming, and a context strategy for long conversations are not here
yet. The plan is in `TODO.md`, next to the code.

## A request, end to end

```cpp
#include <iostream>
#include <memory>

#include "OpenAnima/Core/NPCBrain.h"
#include "OpenAnima/Http/CurlHttpTransport.h"
#include "OpenAnima/Provider/ProviderConfig.h"
#include "OpenAnima/Provider/RestProvider.h"

int main() {
    using namespace OpenAnima;

    const Provider::ProviderConfig config = Provider::ProviderConfig::fromEnvironment();
    auto provider = std::make_shared<Provider::RestProvider>(config, Http::CurlHttpTransport::create());

    Core::NPCBrain brain{provider};
    brain.setSystemPrompt("You are a blacksmith in a small village.");

    const Core::Completion answer = brain.ask("Do you have any iron daggers?");
    std::cout << answer.text << '\n';

    if (answer.finishReason == Core::FinishReason::Length) {
        std::cout << "the answer was cut off\n";
    }
}
```

The configuration comes from four environment variables, of which two are required:

| Variable | Required | Default |
| --- | --- | --- |
| `OPENANIMA_BASE_URL` | yes | - |
| `OPENANIMA_MODEL` | yes | - |
| `OPENANIMA_API_KEY` | no | empty, sent only when set |
| `OPENANIMA_TIMEOUT_SECONDS` | no | `60` |
| `OPENANIMA_TEMPERATURE` | no | `0.7` |
| `OPENANIMA_MAX_TOKENS` | no | `512` |

A local server needs no key at all:

```bash
export OPENANIMA_BASE_URL=http://localhost:11434/v1
export OPENANIMA_MODEL=llama3.1
```

## Errors

Every failure is a subclass of `OpenAnima::OpenAnimaError`, so one `catch` covers the whole
library:

| Type | Raised when |
| --- | --- |
| `ConfigError` | A required variable is missing, or a value is out of range |
| `TransportError` | The request never arrived: DNS, TLS, refused connection, timeout |
| `ProviderError` | The provider answered with an error, carrying its HTTP status |
| `ParseError` | A response arrived but could not be read |
