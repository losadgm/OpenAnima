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
- A reply in three variables: `askTurn()` asks for `reasoning`, `dialogue` and `actions` in one
  document, asked for in the schema kept in `data/reply-schema.json` and with the actions of an
  `ActionCatalogue` filled in, and returns them as `Core::NpcReply`. The dialogue
  is reported while it is written and the whole document is kept as the turn, so the NPC reads
  its own thoughts back.
- One provider interface, `OpenAnima::Provider::IProvider`, and a REST implementation for the
  OpenAI chat-completions contract. Ollama, llama.cpp and vLLM speak it too.
- One HTTP boundary, `OpenAnima::Http::HttpTransport`, so providers can be tested without a
  network and the socket code lives in exactly one place.
- Configuration handed over in code as a `Provider::RestProviderConfig`, with every numeric
  setting checked against its range. The library reads nothing from the environment: where a
  value comes from is the decision of whoever builds the provider.
- An umbrella header, `OpenAnima/OpenAnima.h`, and a `makeNpc(config)` entry point that wires
  the default stack in one call.

What is still missing is a strategy for the context of a long conversation, which grows until
it no longer fits and is not trimmed yet. [The roadmap](roadmap.html) is what comes next.

## A request, end to end

```cpp
#include <iostream>

#include "OpenAnima/OpenAnima.h"

int main() {
    using namespace OpenAnima;

    Provider::RestProviderConfig config;
    config.baseUrl = "http://localhost:11434/v1";
    config.model = "qwen2.5:7b";

    Core::NPCBrain brain = makeNpc(config);
    brain.setSystemPrompt("You are a blacksmith in a small village.");

    const Core::Completion answer = brain.ask("Do you have any iron daggers?");
    std::cout << answer.text << '\n';

    if (answer.finishReason == Core::FinishReason::Length) {
        std::cout << "the answer was cut off\n";
    }
}
```

One include, a configuration and one line of setup. The library takes the values and nothing
else, so where they come from is a choice of the caller: the clients in `examples/` read their
own environment variables, a test passes literals. When the pieces have to be told apart (a
test, a local model, a custom transport), they are still there, one per header; `makeNpc()`
just wires the default stack.

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

Stopping a turn mid-answer is `brain.cancel(turn)`. The call stops, whatever the model had
produced arrives with the `Finished` event marked `FinishReason::Cancelled`, and the
conversation is left as it was.

What comes with it:

- One turn at a time per brain. A second `askAsync()` while one is in flight throws, and
  `isBusy()` answers first when the client wants to ask rather than catch.
- The conversation belongs to the caller's thread: the turn gets a copy of it, and the history
  is written when the last event of the turn has been collected. A client that draws an answer
  decides when the turn becomes part of the conversation, which is also what keeps the turns
  in the order the player saw them.
- A turn that failed or was cancelled leaves the conversation as it was, the same way a failed
  `ask()` does: nothing of a turn nobody finished is written, and the next prompt goes with the
  context the player had before it.
- What `timeoutSeconds` bounds is the silence, not the whole exchange: a model that thinks for a
  minute is fine, one that goes quiet for a minute is not.
- A heartbeat from the server, a `data` field with nothing after it, is dropped. A chunk that
  cannot be read at all fails the turn with a `ParseError` rather than half an answer.

The configuration is a struct, and the defaults are picked so that a local model needs two
lines of it:

| Field | Required | Default |
| --- | --- | --- |
| `baseUrl` | yes | - |
| `model` | yes | - |
| `apiKey` | no | empty, sent only when set |
| `timeoutSeconds` | no | `60`, from 1 to 3600 |
| `temperature` | no | `0.7`, from 0 to 2 |
| `maxTokens` | no | `512`, from 1 to 200000 |

A local server needs no key at all:

```cpp
Provider::RestProviderConfig config;
config.baseUrl = "http://localhost:11434/v1";
config.model = "llama3.1";
```

The clients in `examples/` fill the same struct from their own variables, prefixed with the
name of the client: `OPENANIMA_CLI_*` for the command line and `OPENANIMA_TUI_*` for the
console one, so the two can point at different models. [The harnesses page](harness.html) has
the names.

## Errors

Every failure is a subclass of `OpenAnima::OpenAnimaError`, so one `catch` covers the whole
library:

| Type | Raised when |
| --- | --- |
| `ConfigError` | A required variable is missing, or a value is out of range |
| `TransportError` | The request never arrived: DNS, TLS, refused connection, timeout |
| `ProviderError` | The provider answered with an error, carrying its HTTP status |
| `ParseError` | A response arrived but could not be read |
