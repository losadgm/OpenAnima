# OpenAnima

C++17 middleware for conversational NPCs: agents that talk to the player, react to the world
around them and talk to each other.

An NPC is not a chat box. It plays a persona, remembers what happened, and has to stay coherent
while the player, the environment and the other agents keep talking to it. The library holds
that state, decides what the model needs to see next, and turns the answer into something the
host application can act on. Which provider, which transport, and what to do with the text are
decisions of the application, not of the library.

The conversation is one of three channels an NPC uses:

| Channel | What it carries | Where it stands |
| --- | --- | --- |
| The player | dialogue turns, with a persona and a history behind them | in place |
| The world | events the agent perceives and reacts to | planned, v0.4.0 |
| Other NPCs | shared rooms, and a ledger of who said what | planned, v0.6.0 |

- [Development guide](development.html) - build it, test it, and how these pages are built.
- [API reference](api/index.html) - the classes, in Doxygen form, generated from the headers.

## What it does today

- A conversation object, `OpenAnima::Core::NPCBrain`, with a system prompt, a history, and a
  call that sends a prompt and returns the answer. The same call takes a sink and reports the
  text while the provider is still producing it, piece by piece.
- One provider interface, `OpenAnima::Provider::IProvider`, and a REST implementation for the
  OpenAI chat-completions contract. Ollama, llama.cpp and vLLM speak it too.
- One HTTP boundary, `OpenAnima::Http::HttpTransport`, so providers can be tested without a
  network and the socket code lives in exactly one place.
- Configuration read from the environment, validated, with the ranges checked.
- An umbrella header, `OpenAnima/OpenAnima.h`, and a `makeNpc()` entry point that wires the
  default stack in one call.

Asynchronous calls and a context strategy for long conversations are not here yet: `ask()`
blocks until the whole answer has arrived, even when it has been reporting the answer piece by
piece along the way. [The roadmap](roadmap.html) is what comes next.

## A request, end to end

```cpp
#include <iostream>

#include "OpenAnima/OpenAnima.h"

int main() {
    using namespace OpenAnima;

    Core::NPCBrain brain = makeNpc();
    brain.setSystemPrompt("You are a blacksmith in a small village.");

    const Core::Completion answer = brain.ask("Do you have any iron daggers?");
    std::cout << answer.text << '\n';

    if (answer.finishReason == Core::FinishReason::Length) {
        std::cout << "the answer was cut off\n";
    }
}
```

One include, one line of setup. When the pieces have to be told apart (a test, a local model,
a custom transport), they are still there, one per header; `makeNpc()` just wires the default
stack.

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
