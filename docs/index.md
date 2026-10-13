# OpenAnima

C++17 middleware for a world of agents: NPCs that talk to the player and reason about what they
perceive, information sources that log what happens, and senses that decide what each NPC makes
of it.

An NPC is not a chat box. It plays a persona, remembers what happened, and has to stay coherent
while the player, the world and the other agents keep talking to it. The library holds that
state, decides what the model needs to see next, and turns the answer into something the host
application can act on. Which provider, which transport, and what to do with the text are
decisions of the application, not of the library.

The conversation is one of three channels an NPC uses:

| Channel | What it carries | Where it stands |
| --- | --- | --- |
| The player | dialogue turns, with a persona and a history behind them | in place |
| The world | what the application publishes into its sources, and what each NPC perceives of it | planned, v0.5.0 |
| Other NPCs | shared rooms, and a ledger of who said what | planned, v0.7.0 |

- [Development guide](development.html) - build it, test it, and how these pages are built.
- [Harnesses](harness.html) - the two clients that ship with the library, and what the console
  one is meant to be.
- [API reference](api/index.html) - the classes, in Doxygen form, generated from the headers.

## What it does today

- A conversation object, `OpenAnima::Core::NPCBrain`, with a system prompt, a history and a
  call that sends a prompt and returns the answer. The same call takes a sink and reports the
  text while the provider is still producing it, and a turn can also be asked for without
  waiting and collected as it goes.
- Structured answers: a call asks for a JSON object or for one that follows a JSON Schema with
  `Core::ResponseFormat`, and the answer arrives parsed as well as raw. A provider that does
  not know the field is still checked: an answer that is not the JSON that was asked for is a
  `ParseError`.
- One provider interface, `OpenAnima::Provider::IProvider`, and a REST implementation for the
  OpenAI chat-completions contract. Ollama, llama.cpp and vLLM speak it too.
- One HTTP boundary, `OpenAnima::Http::HttpTransport`, so providers can be tested without a
  network and the socket code lives in exactly one place.
- Configuration read from the environment, validated, with the ranges checked.
- An umbrella header, `OpenAnima/OpenAnima.h`, and a `makeNpc()` entry point that wires the
  default stack in one call.

What is still missing is a strategy for the context of a long conversation, which grows until
it no longer fits and is not trimmed yet. [The roadmap](roadmap.html) is what comes next.

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

## Streaming and asynchronous calls

The same turn can be read while it is being written, and asked for without waiting for it:

```cpp
// Piece by piece, as they arrive, on the thread of the caller.
brain.ask("Do you have any iron daggers?",
          [](std::string_view piece) { std::cout << piece << std::flush; });

// Or on a thread of the brain's own, collected when it suits the caller.
const Core::RequestId turn = brain.askAsync("Do you have any iron daggers?");
for (;;) {
    const std::optional<Core::Event> event = brain.poll(std::chrono::milliseconds{100});
    if (!event.has_value()) {
        continue;  // Nothing happened yet.
    }
    if (event->kind == Core::EventKind::Delta) {
        std::cout << event->text << std::flush;
    }
    if (event->kind == Core::EventKind::Finished || event->kind == Core::EventKind::Failed) {
        break;
    }
}
```

Stopping a turn mid-answer is `brain.cancel(turn)`. The stream ends, whatever the model had
produced by then is kept, and the turn arrives as a `Finished` event marked
`FinishReason::Cancelled`.

What comes with it:

- One turn at a time per brain. A second `askAsync()` while one is in flight throws, and
  `isBusy()` answers first when the client wants to ask rather than catch.
- The conversation belongs to the caller's thread: the turn gets a copy of it, and the history
  is written when the last event of the turn has been collected. A client that draws an answer
  decides when the turn becomes part of the conversation, which is also what keeps the turns
  in the order the player saw them.
- A turn that failed leaves the conversation as it was, the same way a failed `ask()` does. One
  that was cancelled keeps what had been produced, so the agent remembers what the player
  actually read.
- What `OPENANIMA_TIMEOUT_SECONDS` bounds is the silence, not the whole exchange: a model that
  thinks for a minute is fine, one that goes quiet for a minute is not.
- A heartbeat from the server, a `data` field with nothing after it, is dropped. A chunk that
  cannot be read at all fails the turn with a `ParseError` rather than half an answer.

The configuration comes from six environment variables, of which two are required:

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
