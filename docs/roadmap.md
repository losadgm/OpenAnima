# OpenAnima roadmap

What the library is meant to become, milestone by milestone.

- `[x]` in place
- `[~]` partially in place; the note says what is missing
- `[ ]` not started

The scope, in one line: middleware for conversational NPCs that talk to the player, react to the
world and talk to each other. Each NPC is an agent with a persona, a memory and a place in the
world, and the library keeps that state coherent across the three channels. The channel with the
player works end to end; the other two drive the milestones below.

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

- [ ] Ask the model for structured output and parse it with nlohmann/json.
  - nlohmann is in the build, but only for the API envelope: the request body and the response
    parsing. Nothing structures the model's own content yet.
- [ ] Isolate the output variables: `reasoning`, `dialogue`, `actions`.
  - This is what turns an answer into something an NPC can act on: what it thought, what it
    says, and what it does. `Completion::text` is still opaque text, and `Role::Tool` exists in
    the enum with nothing producing or consuming it.
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

## v0.4.0 - Memory and environment perception

- [ ] FIFO short-term memory with a configurable message limit.
  - `history` grows without bound: no window, no truncation, no summarisation, so a long session
    will eventually exceed the model context. First priority overall, now that the asynchronous
    core is in place.
- [ ] Dynamic system prompt assembler.
  - `setSystemPrompt()` stores one fixed string, deliberately kept out of the history so the
    persona can change mid-conversation. Nothing assembles it from state or events yet.
- [ ] Event injection (`NPCBrain::injectEvent`).
  - The world channel: the host application reports what happened around the agent, so the agent
    can react to it instead of only answering questions.

## v0.5.0 - State machine and narrative agenda

- [ ] FSM for emotional or alert states that alter the agent's base directives.
- [ ] Narrative goals queue (temporary instructions injected into the prompt).
- [ ] Verification logic (mark a goal as met by analysing the agent output).

## v0.6.0 - Multi-agent architecture (pub/sub)

- The other-NPCs channel: several agents in one room, each hearing only what concerns it.
- [ ] `ConversationManager` (virtual room coordinator).
- [ ] Shared ledger (event and dialogue history for everyone present).
- [ ] Passive attention heuristic filter to wake only the agents involved.

## v0.7.0 - Native local inference

- [ ] llama.cpp as an optional build dependency.
  - Note the distinction: `RestProvider` already talks to a llama.cpp *server*. What is missing
    here is in-process inference with no intermediate server, which is what a shipped game
    needs.
- [ ] `LlamaCppProvider` for direct CPU/Vulkan execution.
- [ ] GBNF grammar support at the tokeniser level, for reliable JSON.

## v0.8.0 - Concurrency safety and scheduler

- [ ] Global inference queue (avoid VRAM contention when several agents run at once).
  - Each brain runs its turn on a thread of its own today, which is enough for a client driving
    one agent and not enough for a room of them sharing one machine.
- [ ] Strict thread safety through mutexes and locks on shared resources and memory.
  - A brain is driven from one thread: the asynchronous calls run a turn on a thread of the
    library's own and hand its events to whoever collects them, but two threads calling
    `ask()` on the same brain would still race on the history. The transport is closer, with
    one curl handle per call, `CURLOPT_NOSIGNAL` and a `std::call_once` initialisation.
- [ ] Barks system (filler lines while inference is slow).

## v0.9.0 - Polish, profiling and examples

- [~] Interactive CLI harness under `examples/`.
  - `examples/cliHarness` is interactive, keeps the conversation across turns, streams the
    answer as it arrives and loads `.env` on its own, but it drives a single agent with no
    world around it and it paints one line at a time.
- [~] Terminal client under `examples/`.
  - `examples/tuiHarness`, designed in [the harnesses page](harness.html): a full screen with
    a conversation of its own to scroll, a prompt pinned at the bottom with its command
    palette, and a world of NPCs reached by identifier with `/create`, `/enter`, `/npcs` and
    `/leave`. Built over its own terminal layer (Unix termios, Windows console) and split into
    the pure parts (`app/` for the model and the keys, `screen/` for the rendering) and the
    shell that runs them. What is left is the client test over a pseudo-terminal inside the
    suite, and letting several NPCs share one conversation, which needs the library to carry
    it.
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
2. **One queue for every brain** (v0.8.0). Each brain runs its turn on a thread of its own
   today, which is enough for a client driving one agent and not enough for a room of them
   sharing one machine.
3. **Drop `NPCBrain::ping()`** before the API freeze. It only proves that the library links.

`CMakeLists.txt` carries version 0.2.0: the milestone is closed, with the asynchronous core
verified against a real provider before the number moved.
