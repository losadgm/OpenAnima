# OpenAnima roadmap

Audited against the code and updated on 2026-10-11.

Legend:

- `[x]` done
- `[~]` partially done, the note says what is left
- `[ ]` pending

The plan was written for `OpenNPC`. The project is `OpenAnima` now and a few names
were chosen differently on the way. The "Renamed and reshaped" section at the end
lists every change and the reason for it.

## v0.1.0 - Foundations and scaffolding

- [x] CMake configuration (FetchContent, dynamic dependencies).
  - nlohmann/json v3.12.0, Catch2 v3.16.1 and libcurl. The system libcurl is used when
    `find_package` finds it; otherwise curl is built from source with Schannel on
    Windows and OpenSSL elsewhere, so no runner needs a manual install.
- [x] CI/CD pipeline in GitHub Actions (cross-platform build validation).
  - `.github/workflows/ci.yml`, four jobs: ubuntu and windows, each Debug and Release.
- [x] Base layout and a base unit test (Catch2).
  - The layout evolved into packages, see "Renamed and reshaped". 25 test cases, all
    offline.

## v0.2.0 - Abstraction and asynchrony

- [x] Agnostic data structures.
  - `Core::ChatMessage` with `Role`, and `Core::Completion` with `FinishReason` and
    `TokenUsage`. `Completion` carries the answer text, the model that answered, why
    generation stopped and the reported token usage, which is empty when the provider
    reports none.
- [x] Abstract provider interface (Strategy pattern).
  - `OpenAnima::Provider::IProvider`. `complete()` returns a `Completion`, so the
    metadata survives the call instead of being thrown away while parsing.
- [x] REST provider built on libcurl (Ollama, OpenAI, LM Studio).
  - `Provider::RestProvider` over `Http::CurlHttpTransport`. Verified against a real
    OpenAI call and against Ollama's contract. LM Studio speaks the same one.
- [ ] Asynchronous core.
  - `ask()` still blocks the caller. Nothing asynchronous exists in the codebase: no
    thread, no future, no mutex. This is the last item of the milestone and the one that
    closes the promise the project makes about not blocking the client.

## v0.3.0 - Strict parsing (data structuring)

- [ ] Ask the model for structured output and parse it with nlohmann/json.
  - nlohmann is already in the build, but only for the API envelope: the request body
    and the response parsing. Nothing structures the model's own content yet.
- [ ] Isolate the output variables: `reasoning`, `dialogue`, `actions`.
  - `Completion::text` is still opaque text. `Role::Tool` exists in the enum and nothing
    produces or consumes it.
- [ ] Reject malformed answers and fall back.
  - Validation exists: `ParseError` and `ProviderError` reject anything malformed or
    rejected, and the error carries the HTTP status. The retry half became its own item
    below, because it is a feature and not a tail of the validation.

## v0.3.1 - Resilience

- [ ] Retry policy for transient failures (429, 5xx, timeouts) with backoff and a
      configurable attempt limit.
  - Today a 429 or a 500 surfaces to the caller as an exception on the first try.
  - Open question: retries could live in `RestProvider`, or wrap `HttpTransport` as a
    decorator. The decorator keeps `RestProvider` free of the policy, but needs the
    transport to know which failures are worth repeating.

## v0.4.0 - Memory and environment perception

- [ ] FIFO short-term memory with a configurable message limit.
  - `history` grows without bound. There is no window, truncation or summarisation, so a
    long session will eventually exceed the model context. Second priority overall,
    after the asynchronous core.
- [ ] Dynamic system prompt assembler.
  - `setSystemPrompt()` stores one fixed string, deliberately kept out of the history so
    the persona can change mid-conversation. There is no assembly from state or events.
- [ ] Event injection (`NPCBrain::injectEvent`).

## v0.5.0 - State machine and narrative agenda

- [ ] FSM for emotional or alert states that alter the agent's base directives.
- [ ] Narrative goals queue (temporary instructions injected into the prompt).
- [ ] Verification logic (mark a goal as met by analysing the agent output).

## v0.6.0 - Multi-agent architecture (pub/sub)

- [ ] `ConversationManager` (virtual room coordinator).
- [ ] Shared ledger (event and dialogue history for everyone present).
- [ ] Passive attention heuristic filter to wake only the agents involved.

## v0.7.0 - Native local inference

- [ ] llama.cpp as an optional build dependency.
  - Note the distinction: `RestProvider` already talks to a llama.cpp *server*. What is
    missing here is in-process inference with no intermediate server.
- [ ] `LlamaCppProvider` for direct CPU/Vulkan execution.
- [ ] GBNF grammar support at the tokeniser level, for reliable JSON.

## v0.8.0 - Concurrency safety and scheduler

- [ ] Global inference queue (avoid VRAM contention when several agents run at once).
- [ ] Strict thread safety through mutexes and locks on shared resources and memory.
  - `NPCBrain` is not thread-safe today: two concurrent `ask()` calls on the same brain
    would corrupt the history. The transport is closer to it, with one curl handle per
    call, `CURLOPT_NOSIGNAL` and a `std::call_once` initialisation.
- [ ] Barks system (filler lines while inference is slow).

## v0.9.0 - Polish, profiling and examples

- [~] Interactive CLI harness under `examples/`.
  - `examples/cli_harness` is interactive, keeps the conversation across turns and loads
    `.env` on its own, but it drives a single agent.
- [ ] Logging hook with levels (Trace, Debug, Warn, Error).
  - Today the only output is a stderr note from the harness and the exception messages.
- [ ] Exhaustive memory leak analysis (AddressSanitizer or Valgrind).
  - No sanitizer configuration in CMake or in CI. `curl_global_cleanup` is registered at
    exit, which is not the same as having measured anything.
- [ ] Tests for the `.env` loader of the harness.
  - It is the only component verified by hand instead of by the suite.

## v1.0.0 - Release candidate (MVP)

- [ ] Full public API freeze.
- [~] Documentation.
  - The Doxygen half is done: `docs/Doxyfile.in`, Doxygen comments in every public
    header, and a `docs` target behind `-DOPENANIMA_ENABLE_DOCS=ON`. What is left is
    publishing the output, plus a written integration guide.
- [ ] Packaging and automatic prebuilt binary publication through GitHub Releases.
  - No `install()` rules and no export of targets, so consuming the library from another
    CMake project requires FetchContent. There is no release workflow either.

## Renamed and reshaped

| In the plan | Now | Why |
| --- | --- | --- |
| `OpenNPC` | `OpenAnima` | the repository was renamed |
| `LLMResponse` | `Core::Completion` | it is the result of `complete()`, and it does not tie the library to LLMs |
| `ILLMProvider` | `Provider::IProvider` | keeps the `I` prefix, drops the stutter inside its own package |
| `RestAPIProvider` | `Provider::RestProvider` | shorter; `Rest` is still the distinguishing word |
| `pensamiento_interno`, `dialogo`, `acciones_ejecutables` | `reasoning`, `dialogue`, `actions` | artifacts in this repository are written in English |
| `inject_event` | `injectEvent` | the project uses camelBack for functions |
| "Pitchfork layout" | package layout with namespaces | a flat `include/` and `src/` does not scale with the modules above |
| "`std::async` and `std::future`" | worker threads handing back futures | `std::async` spawns a thread per call, which is the wrong shape for many agents |
| "MkDocs manuals" | published Doxygen output plus an integration guide | the reference is generated from the headers; a hand-maintained site would drift |
| "levelled logging system" | a logging hook with levels | a library should let the host application decide where the logs go |
| retries inside v0.3.0 | their own item under v0.3.1 | the validation was done and delivered; retries were never started |

## Already done and not in the original plan

- Dependabot for GitHub Actions and Renovate for the CMake FetchContent pins.
- A dependency graph workflow that submits the CMake dependencies.
- Branch protection: the `protect-default` and `protection-strict` rulesets, plus
  CODEOWNERS and code owner review.
- Module packages with their own namespaces: `OpenAnima::Core`, `OpenAnima::Http`,
  `OpenAnima::Provider`, with the error taxonomy shared at the root.
- Typed failures: `ConfigError`, `TransportError`, `ProviderError` carrying the HTTP
  status, and `ParseError`, all derived from `OpenAnimaError`.
- Provider configuration read from the environment, with validation and range checks.
- Editor setup: clang-format, clang-tidy, clangd, launch configurations and tasks.
- A `docs` target wired to the Doxygen template, which existed but was unused.
- The reference itself: a page per file and per package, the source listings removed, and
  a CI job that fails on Doxygen warnings.
- The documentation site: `docs/` rendered with pandoc, the reference published next to it
  under `/api/`, deployed to Pages on every push to the default branch.

## Notes

- The version in `CMakeLists.txt` stays at 0.1.0 while v0.2.0 is three quarters done.
  It should move to 0.2.0 when the asynchronous core lands.
- The two gaps worth attacking first:
  1. The asynchronous core. It is the promise of the project, and it is cheap to add now
     because it is isolated behind a single call to `IProvider::complete()`.
  2. Unbounded context growth. It is the first thing that will bite in real use, and the
     FIFO queue from v0.4.0 is already the planned fix.
- `NPCBrain::ping()` is a leftover from the first scaffold. It exists only to prove the
  library links, and it should go before the API freeze.
