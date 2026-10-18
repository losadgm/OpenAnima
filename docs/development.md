# Development guide

How to build the library, run the tests, and publish these pages.

## Requirements

- CMake 3.20 or newer, and a C++17 compiler (clang or MSVC are the ones covered by CI).
- Ninja, or any other generator you like.
- Doxygen and pandoc, only to build this site.
- Nothing else. nlohmann/json, Catch2 and libcurl are fetched at configure time from pinned
  tags when the system does not provide them.

## Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests never touch the network. `FakeHttpTransport` and `FakeProvider` live in
`tests/support/`, and a test that needs a provider uses one of them. Keep it that way: a test
that reaches for a real endpoint is a test that fails on a plane.

## Options

| Option | Default | What it does |
| --- | --- | --- |
| `OPENANIMA_BUILD_EXAMPLES` | `ON` | Builds the clients under `examples/` |
| `OPENANIMA_ENABLE_CLANG_TIDY` | `OFF` | Runs clang-tidy while compiling |
| `OPENANIMA_ENABLE_DOCS` | `OFF` | Adds the `docs` target, which generates the API reference |

No option changes the public headers. What a consumer can call is the same everywhere.

## Layout

```
include/OpenAnima/     public headers
    OpenAnima.h        umbrella header, and the makeNpc() entry point
    Core/              the NPC and the conversation types
    Http/              the transport interface and the libcurl implementation
    Provider/          the backend interface, the settings, the REST provider
src/OpenAnima/         the implementation, mirroring the headers
tests/                 Catch2 tests, one directory per package
examples/cliHarness/  a runnable client: takes its settings from flags and .env, sends a
                      prompt, prints the turn in the order of the reply contract
examples/common/      shared by both clients: the loader for `.env`
examples/tuiHarness/  the terminal client: full screen, transcript, mouse and clipboard
data/                  the words and the contract: the prompts, the reply schema and the client texts
docs/                  these pages, the Doxyfile template and the site template
.github/scripts/       helpers used by the pipeline, including the site builder
.github/instructions/  the conventions of this repository, for editor tooling
```

A header and its implementation share a path: `OpenAnima/Core/NPCBrain.h` is implemented by
`OpenAnima/Core/NPCBrain.cpp` under `src/`.

`data/` holds the words and the contract: `prompts.json` for the instruction the library sends,
`reply-schema.json` for the schema the answer is asked for in, and `tui-texts.json` for what the
terminal client says. CMake embeds each one in the target that uses it, so none of them opens a
file at run time: editing the json and building again is the whole workflow. The library reads
them in `ActionCatalogue`, where the schema is embedded as it is and the instruction takes the
description of every property from it, so a property is described once. The client reads its
texts once through `Domain::texts()`, where a `{name}` in a text is filled with `Domain::fill()`.

## Conventions

- Public API lives in `include/OpenAnima/` and in the `OpenAnima::` namespaces. Anything else
  is an implementation detail and is not installed.
- Style is enforced by `.clang-format` (LLVM base, 100 columns) and `.clang-tidy`. Both read
  the compile database, so configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (already the
  default here) before running tidy.
- Identifiers, comments and documentation are written in English. ASCII only.
- `assert` is for broken preconditions: null, empty, malformed. A rule of the domain is an
  exception, not an assertion.

## Threads

`Core::NPCBrain::askAsync()` runs a turn on a thread of the library's own, and the target
links `Threads::Threads` publicly, so a consumer that links `OpenAnima::OpenAnima` gets it
without asking. Nothing else starts a thread: the synchronous calls, the provider and the
transport work on the thread that called them, and the conversation is written on that thread
when the events of a turn are collected.

## Turns and their rounds

A turn is the exchange the conversation adds up as one: the prompt, and the reply the model
answers it with. Today a turn is one request, and the reply is one document with the three
variables of the contract in it: `reasoning`, `dialogue` and `actions`. That is deliberate.

- A local model pays twice for every call: the context is sent again, and its prefill is paid
  again on the same tokens. Two calls a turn is twice the time a turn takes on the machine the
  project targets, and it doubles the VRAM an NPC holds while it answers.
- The three variables come out of one sample, so the words cannot disagree with the thought
  that produced them.
- The document is the assistant message of the conversation, so the NPC reads its own
  reasoning on the next turn without a second place to keep it in.
- Cancelling keeps one meaning: it stops the call, keeps what the dialogue had said, and
  leaves the conversation as it was.

A round is a request inside a turn. Rounds are what a deeper turn needs, and a round earns its
cost when it brings something new back: memory, the world, another NPC. Reasoning again over
the same context only reorders what is already there. The plan is in "Beyond the MVP" of the
roadmap, and the invariants it has to respect are these.

- Intermediary rounds are not turns. They do not enter the history, and only the last reply is
  kept. The bookkeeping of the turn in flight is `NPCBrain::PendingTurn`, and `poll()` is the
  only place that writes a turn into the conversation.
- An observation comes back as a `Role::Tool` message after the document of the round that
  asked for it, and the next round reads the conversation it had plus those two messages.
- Only the last round streams. A delta of an intermediary round is a fragment of JSON and not
  dialogue, and letting it through would paint the document on the client.
- A turn still has one cancellation. Stopping between rounds drops all of them, and the
  dialogue rescued from a cut turn is the one of the last round.

What is missing to turn any of that on is the source of the observations, which is the world of
`v0.5.0`, and the stop condition: a number of rounds, a word from the model or a judge. Until
those exist a turn has one round, and the client sees no difference.

## Documentation

There is one source of truth per thing:

- The API reference is generated by Doxygen from the headers in `include/`. The Doxygen
  comments next to a declaration are the documentation for it, and that is the only place it
  is written down.
- These pages are the Markdown files in `docs/`, rendered by `.github/scripts/build_docs.py`.
- The roadmap is `docs/roadmap.md`, published as one more page.

All of it is published together at
[losadgm.github.io/OpenAnima](https://losadgm.github.io/OpenAnima/).

### Building the site locally

```bash
cmake -S . -B build -DOPENANIMA_ENABLE_DOCS=ON
cmake --build build --target docs          # writes build/docs/html
python3 .github/scripts/build_docs.py      # writes build/site
python3 -m http.server -d build/site 8000  # http://localhost:8000
```

The first step needs Doxygen and the second needs pandoc. The script itself only needs
python3, no packages to install.

### Adding a page

1. Write it as Markdown in `docs/`. The file name becomes the URL: `docs/naming.md` is
   published as `naming.html`.
2. Add a link to the navigation in `docs/site/template.html`, otherwise the page is reachable
   only by URL.
3. Every `docs/*.md` is rendered, so nothing else is needed.

Note that pandoc does not run diagram tools. A diagram has to be committed as an image (SVG
is preferred: it stays readable when zoomed) and linked from the Markdown.

Pandoc appends its own stylesheet for highlighted code after `docs/site/style.css`, so a rule
there and a rule here that select the same element are won by that one, and by class rather
than by element. A rule that has to hold for code blocks names the class: `pre.sourceCode`,
not `pre`. The horizontal scroll of a long line is one of those.

## Continuous integration

| Workflow | What it checks |
| --- | --- |
| `ci.yml` | Builds and tests Debug and Release on Ubuntu and Windows, and builds the API reference with warnings treated as errors |
| `pages.yml` | Publishes these pages and the reference to GitHub Pages on every push to `main` |
| `dependency-graph.yml` | Submits the CMake dependencies to the dependency graph |

Renovate keeps the pins in `CMakeLists.txt` current, Dependabot the GitHub Actions versions.
