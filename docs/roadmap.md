# OpenAnima roadmap

What the library is meant to become, milestone by milestone.

- `[x]` in place
- `[~]` partially in place; the note says what is missing
- `[ ]` not started

The scope, in one line: middleware for a world of agents. The host application creates a world
and, inside it, actors: NPCs, which converse and reason, and information sources, logs the
application publishes what happens in the world into. Everything in a world can interact, but
nothing in it knows everything: an NPC reads the sources its host subscribed it to, and what
reaches its context is not the record but what its senses make of it. The channel with the player
works end to end; the world and the other agents drive the milestones below.

## The shape of a world

- A **world** is what the application creates, and what everything else lives in.
- An **actor** is what a world holds: an **NPC**, an entity with a persona, a memory and senses,
  or an **information source**, a log the application publishes what happens in the world into.
- An NPC does not read the world. The application **subscribes** it to the sources it should
  follow, and to those only, so two of them in the same room can stand next to two different
  accounts of what is happening.
- A **sense** belongs to the NPC and decides how much of a source, and in which form, reaches
  its context. The source keeps the whole record; the NPC gets what its senses made of it.

An apple falls off a table, and the source that logs it keeps the moment and the millimetres. An
NPC with sight and hearing, subscribed to that source, reads that something small landed nearby
when it is looking elsewhere, and reads an apple of a colour, falling in a way, at about the
distance its eye can tell when it happens to be looking that way. The record is exact; the
context is what the senses let through.

## v0.1.0 - Foundations and scaffolding

- [x] CMake configuration (FetchContent, dynamic dependencies).
  - nlohmann/json v3.12.0, Catch2 v3.16.1 and libcurl. The system libcurl is used when
    `find_package` finds it; otherwise curl is built from source with Schannel on Windows and
    OpenSSL elsewhere, so no runner needs a manual install.
- [x] CI/CD pipeline in GitHub Actions (cross-platform build validation).
  - `.github/workflows/ci.yml`: ubuntu and windows, each Debug and Release, plus a job that
    builds the documentation and fails on a Doxygen warning.
- [x] Repository automation.
  - Dependabot for the workflow actions, Renovate for the CMake pins, a workflow that submits the
    dependency graph, and branch protection with code owner review.
- [x] Packages and namespaces.
  - `OpenAnima::Core`, `OpenAnima::Http` and `OpenAnima::Provider`, with the error taxonomy
    shared at the root. A public header and its implementation share a path, so
    `Core/NPCBrain.h` is implemented by `Core/NPCBrain.cpp`.
- [x] Base layout and a base unit test suite (Catch2).
  - The suite is Catch2, and no case in it touches the network.
- [x] Editor and local tooling.
  - clang-format, clang-tidy, clangd, build and debug configurations, and a compile database
    that both tools read.

## v0.2.0 - Abstraction and asynchrony

- [x] Agnostic data structures.
  - `Core::ChatMessage` with `Role`, and `Core::Completion` with `FinishReason` and
    `TokenUsage`. `Completion` carries the answer text, the model that answered, why generation
    stopped and the reported token usage, which is empty when the provider reports none.
- [x] Abstract provider interface (Strategy pattern).
  - `OpenAnima::Provider::IProvider`. `complete()` returns a `Completion`, so the metadata
    survives the call instead of being thrown away while parsing.
- [x] REST provider built on libcurl (Ollama, OpenAI, LM Studio).
  - `Provider::RestProvider` over `Http::CurlHttpTransport`. Verified against a real OpenAI call
    and against Ollama's contract. LM Studio speaks the same one.
- [x] Provider configuration read from the environment, and validated.
  - `OPENANIMA_BASE_URL` and `OPENANIMA_MODEL` are required, the rest have defaults, and a value
    out of range is rejected with a `ConfigError` that names the variable and the bounds. The
    reader takes a lookup function, so a test never has to patch the process environment.
- [x] Typed failures.
  - `ConfigError`, `TransportError`, `ProviderError` (which carries the HTTP status) and
    `ParseError`, all derived from `OpenAnimaError`, so one `catch` in the host application
    covers the whole library.
- [x] Streamed answers.
  - `IProvider::complete()` takes a sink and reports every piece as it arrives, and
    `RestProvider` reads the `text/event-stream` the contract answers with, so a caller can
    paint an answer while the model is still writing it. Under it, `HttpTransport` reports the
    status and the body as they land, an exchange can be abandoned through a short write, and
    what is bounded is the silence rather than the whole exchange. A heartbeat (`data` with
    nothing after it) is dropped rather than read as a chunk, and a callback that throws
    cannot escape into libcurl: the failure is raised once the handle has been released.
- [x] Asynchronous core.
  - `NPCBrain::askAsync()` hands a turn to a thread of its own and reports it as events:
    `poll()` collects them, `cancel()` stops a turn and keeps the text it had produced, and
    `isBusy()` says whether one is still open. The conversation is written on the caller's
    thread, when the last event of the turn is collected, so a client that draws an answer
    still owns the order the turns are written in.

## v0.3.0 - Strict parsing (data structuring)

- [x] Ask the model for structured output and parse it with nlohmann/json.
  - `Core::ResponseFormat` travels with the call: free text, a JSON object, or a JSON Schema
    sent as `response_format` where the contract knows the field. The answer comes back read
    as well as raw, and an answer that is not the JSON that was asked for is a `ParseError`
    rather than something the caller discovers later. The contract of an NPC's own answer is
    the next point.
- [x] Isolate the output variables: `reasoning`, `dialogue`, `actions`.
  - `askTurn()` and `askTurnAsync()` ask the answer in a schema built from an
    `ActionCatalogue`, read it back as an `NpcReply` and keep the whole document in the
    history. The contract also travels in the instruction sent with the prompt, so a provider
    that ignores the schema still answers in it, and the dialogue is read out of the document
    while it arrives, so a sink paints words rather than JSON.
- [ ] Reject malformed answers and fall back.
  - The validation half is in place: `ParseError` and `ProviderError` reject anything that
    cannot be read or that the provider refused, and the error carries the HTTP status. The
    fallback is not: a rejected answer reaches the caller with no second attempt.

## v0.3.1 - Resilience

- [ ] Retry policy for transient failures (429, 5xx, timeouts) with backoff and a configurable
      attempt limit.
  - Today a 429, a 500 or a timeout reaches the caller as an exception on the first try.
  - Open question: retries could live in `RestProvider`, or wrap `HttpTransport` as a decorator.
    The decorator keeps `RestProvider` free of the policy, but it needs the transport to know
    which failures are worth repeating.

## v0.4.0 - Memory and knowledge

- [ ] FIFO short-term memory with a configurable message limit.
  - `history` grows without bound: no window, no truncation, no summarisation, so a long session
    will eventually exceed the model context. First priority overall, now that the asynchronous
    core is in place.
- [ ] Dynamic system prompt assembler.
  - `setSystemPrompt()` stores one fixed string, deliberately kept out of the history so the
    persona can change mid-conversation. Nothing assembles it from state or events yet.
- [ ] Beliefs as data, and a policy that keeps the answer inside them.
  - A persona is prose, and prose invites invention: state a past in one sentence and the model
    writes the battle. What an NPC knows goes in as data, with identifiers and apart from the
    persona, and anything outside that list is not known. Over it, a level per category
    (`forbid`, `hedge`, `allow`), written into the prompt and checked on the way out, because a
    prohibition that is not checked is a suggestion. The check is a judgment over a closed set,
    so it does not have to be an LLM: anything that answers only inside the options it is given
    can serve, behind one interface, with a narrow question to the model as the default.
- [ ] Event injection (`NPCBrain::injectEvent`).
  - The world channel in its simplest form: the host application reports what happened around the
    agent, so the agent can react to it instead of only answering questions. It is the pipe the
    world of `v0.5.0` is built on.

## v0.5.0 - The world and perception

- The world channel: what the application publishes into its sources, and what each NPC makes of
  it. It is what the milestones after this one assume.
- [ ] A world that holds its actors.
  - `makeNpc()` hands out a brain on its own today, and the rest of the state stays with the
    application. The world is where the brains, the sources they follow and everything the
    application publishes live together, and it is what the milestones after this one assume.
- [ ] Information sources: the world's logs.
  - An actor that carries records. The application publishes what happens in the world into it, a
    door opening or an apple falling, and the source keeps them whole, in order and exact.
- [ ] Subscriptions: which sources feed each NPC.
  - The application decides, from the moment the NPC is made, and an NPC reads nothing else. It
    is what lets a world be shared without the knowledge being.
- [ ] Senses: what an NPC makes of what it is subscribed to.
  - One per way of perceiving, owned by the NPC, each turning a source record into the
    approximation that reaches the prompt. A pass-through is the first one, so the pipeline
    exists before any sense is clever.
  - Open question: whether a sense is configuration of the NPC or an interface the host
    implements. The apple above wants the second; the first is enough to start.

## v0.6.0 - State machine and narrative agenda

- [ ] FSM for emotional or alert states that alter the agent's base directives.
- [ ] Narrative goals queue (temporary instructions injected into the prompt).
- [ ] Verification logic (mark a goal as met by analysing the agent output).

## v0.7.0 - Multi-agent architecture (pub/sub)

- The other-NPCs channel: several agents in one room, over the subscriptions `v0.5.0` leaves in
  place. The room is shared and the knowledge is not: each agent reads what it is subscribed to,
  and perceives it through senses of its own.
- [ ] `ConversationManager` (virtual room coordinator).
- [ ] Shared ledger (event and dialogue history for everyone present).
- [ ] Passive attention heuristic filter to wake only the agents involved.

## v0.8.0 - Native local inference

- [ ] llama.cpp as an optional build dependency.
  - Note the distinction: `RestProvider` already talks to a llama.cpp *server*. What is missing
    here is in-process inference with no intermediate server, which is what a shipped game
    needs.
- [ ] `LlamaCppProvider` for direct CPU/Vulkan execution.
- [ ] GBNF grammar support at the tokeniser level, for reliable JSON.

## v0.9.0 - Concurrency safety and scheduler

- [ ] Global inference queue (avoid VRAM contention when several agents run at once).
  - Each brain runs its turn on a thread of its own today, which is enough for a client driving
    one agent and not enough for a room of them sharing one machine.
- [ ] Strict thread safety through mutexes and locks on shared resources and memory.
  - A brain is driven from one thread: the asynchronous calls run a turn on a thread of the
    library's own and hand its events to whoever collects them, but two threads calling
    `ask()` on the same brain would still race on the history. The transport is closer, with
    one curl handle per call, `CURLOPT_NOSIGNAL` and a `std::call_once` initialisation.
- [ ] Barks system (filler lines while inference is slow).

## v0.10.0 - Polish, profiling and examples

- [~] Interactive CLI harness under `examples/`.
  - `examples/cliHarness` takes every setting as an option, with its own variables and `.env`
    as the fallback, keeps one conversation per NPC across turns (`/create`, `/enter`, `/npcs`,
    `/leave`, `/clear`), and prints the turn in the order of the reply contract, with the raw
    document behind `--raw` or `/raw`. What is left is a case in the suite that drives the
    binary itself.
- [~] Terminal client under `examples/`.
  - `examples/tuiHarness`, designed in [the harnesses page](harness.html): a full screen with
    a conversation of its own to scroll, a prompt pinned at the bottom with its command
    palette, and a world of NPCs reached by identifier with `/create`, `/enter`, `/npcs` and
    `/leave`, plus `/raw` to read the document the model wrote. Built over its own terminal
    layer (Unix termios, Windows console) and split by what each part is allowed to know:
    `domain/` for the model and the keys, `presentation/` for the frame, `application/` for
    the loop and what the pointer does, and `terminal/` for the console. What is left is the
    client test over a pseudo-terminal inside the suite, and letting several NPCs share one
    conversation, which needs the library to carry it.
  - It is provisional on purpose: the client is where the shape of the library is tried out,
    so it is expected to be reshaped as the API grows towards what the milestones above ask
    for. What it does today is in the harnesses page; nothing here is frozen until the API is.
- [ ] Logging hook with levels (Trace, Debug, Warn, Error).
  - The only output today is a stderr note from the harness and the exception messages. A
    library should let the host application decide where the logs go.
- [ ] Exhaustive memory leak analysis (AddressSanitizer or Valgrind).
  - No sanitizer configuration in CMake or in CI, and `curl_global_cleanup` is registered at
    exit, which is not the same as having measured anything. The one measurement so far was
    made by hand, with a probe outside the suite: it caught a libcurl handle leaked by an
    exception leaving a callback, since fixed, and nothing runs it again on a change.
- [ ] Tests for the `.env` loader of the harness.
  - It is the only component verified by hand instead of by the suite.

## v1.0.0 - Release candidate (MVP)

- [ ] Full public API freeze.
- [~] Documentation.
  - The reference is generated from the headers and published together with the development
    guide and this roadmap at https://losadgm.github.io/OpenAnima/, and CI builds it on every
    change. What is missing is a written guide for integrating the library into a host
    application.
- [ ] Packaging and automatic prebuilt binary publication through GitHub Releases.
  - There are no `install()` rules and no exported targets, so consuming the library from
    another CMake project means FetchContent. There is no release workflow either.

## Next steps

1. **A bounded context** (v0.4.0). The history grows without limit, and the FIFO window is
   already the planned fix. It is the first thing a long session hits.
2. **The world** (v0.5.0). The library creates worlds in the plan and hands out a brain in the
   code: `makeNpc()` is all of it today, and the sources, the subscriptions and the senses the
   milestones above assume are what turns one brain into a world.
3. **One queue for every brain** (v0.9.0). Each brain runs its turn on a thread of its own
   today, which is enough for a client driving one agent and not enough for a room of them
   sharing one machine.
4. **Drop `NPCBrain::ping()`** before the API freeze. It only proves that the library links.

## Beyond the MVP

Not planned, and no milestone: where the project may go once the release candidate is out, in one
place so the ideas are not lost.

- **Long-term memory with retrieval.** The window of `v0.4.0` bounds the conversation, and what
  falls out of it is gone. A diary an NPC writes what it perceives into, and reads back from by
  relevance, is the other half of the memory: the short window keeps the exchange and the diary
  keeps the history. It needs a route for embeddings (the contract has one, and the provider
  interface is chat only today), an index that stays in process so the library stays light, and
  a retrieval policy, which is what separates an NPC that remembers from a search box. A summary
  of the window may be enough before any of that is needed.
- **A turn that thinks in rounds.** One request is one reply today: the model answers once and the
  turn is closed. A deeper conclusion sometimes needs a second pass, either to look at its own
  answer again or, more usefully, to go and get something it did not have - its memory, the world,
  another NPC - and think again with it in hand. Reasoning over the same context reorders what is
  already there rather than finding anything new, so the round earns its cost when it brings an
  observation back, which is the world of `v0.5.0` and the tools behind `Role::Tool`. What is
  missing is the stop condition - a number of rounds, a word from the model itself, or a judge -
  and that is what has kept it out of the stages. What it cannot change: intermediary rounds are
  not turns, they do not enter the history, and only the last reply is kept.

`CMakeLists.txt` carries version 0.2.0: the milestone is closed, with the asynchronous core
verified against a real provider before the number moved.
