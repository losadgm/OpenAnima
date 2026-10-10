# OpenNPC - C++ Cognitive Middleware for AI Agents

[![CI](https://github.com/losadgm/OpenNPC/actions/workflows/ci.yml/badge.svg)](https://github.com/losadgm/OpenNPC/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](#requirements)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

OpenNPC is a general-purpose **C++ middleware** for building agents and NPCs driven by
large language models. It targets *any* client application — simulations, user
interfaces, games or robotics — and its core promise is that **reasoning never blocks the
client's main thread**: inference runs on worker threads while the application keeps
rendering, simulating or responding.

> Status: **early scaffolding (v0.1.0)**. The public API is intentionally tiny today and
> will grow module by module.

## Table of contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Quickstart](#quickstart)
- [Requirements](#requirements)
- [Project layout](#project-layout)
- [Development](#development)
- [Contributing](#contributing)
- [License](#license)

## Overview

Integrating an LLM into real software is not just "call an API and print the answer".
Production clients need non-blocking execution, persistent memory, structured output,
multi-agent coordination and interchangeable inference backends. OpenNPC packages those
concerns behind a small, dependency-light C++ library:

- **Framework-agnostic** — plain C++17, no assumptions about your engine (Godot, Unreal,
  Qt, a headless server, an embedded loop, …).
- **Non-blocking by design** — the cognitive work happens off the caller's thread, so it
  is safe for real-time loops and graphical interfaces.
- **Provider-agnostic** — swap REST APIs, local models or mock providers without touching
  application code.
- **Deterministic parsing** — the model's free-form text is separated from actionable
  commands via a strict JSON contract.

## Architecture

The library is organised into a handful of cooperating modules. They are described here as
the authoritative roadmap; the first scaffold only ships `NPCBrain` and `ChatMessage`.

| Module | Responsibility |
| --- | --- |
| **Strategy Pattern Providers** | Interchangeable inference adapters (REST API, local inference, mock). One `IProvider` interface; the client picks the backend (and can switch at runtime) without recompiling its logic. |
| **Async NPCBrain** | The execution engine. Schedules inference on secondary threads and delivers results back to the client, so real-time and UI applications never block on the main thread. |
| **Context & Memory Manager** | Owns the dynamic system prompt, the conversation history, a finite-state machine for agent states and injection of environment events into the prompt. |
| **Narrative/Goal Agenda** | A goal system that steers the agent towards specific objectives, forcing it to convey key information or comply with narrative directives. |
| **ConversationManager (Multi-Agent)** | A publish/subscribe bus that coordinates spontaneous interactions between multiple agents and human users. |
| **Strict JSON Parser** | Filters that split conversational output from executable commands: system events, animations, callbacks and other client triggers. |

```
Client app ──▶ Async NPCBrain ──▶ Context & Memory ──▶ Provider (Strategy)
     ▲                │                                        │
     │                ▼                                        ▼
     └──── Strict JSON Parser ◀── ConversationManager ◀── LLM response
                              (Narrative/Goal Agenda)
```

## Quickstart

```bash
# 1. Clone
git clone https://github.com/your-org/OpenNPC.git
cd OpenNPC

# 2. Configure (clang + Ninja; emits compile_commands.json for clangd)
cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_C_COMPILER=clang \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 3. Build (dependencies are fetched automatically)
cmake --build build --parallel

# 4. Run the tests
ctest --test-dir build --output-on-failure

# 5. Run the example harness
./build/examples/open_npc_cli   # -> "OpenNPC inicializado correctamente."
```

Consume it from another CMake project:

```cmake
FetchContent_Declare(OpenNPC GIT_REPOSITORY https://github.com/your-org/OpenNPC.git)
FetchContent_MakeAvailable(OpenNPC)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE OpenNPC::OpenNPC)
```

## Requirements

- **Compiler:** Clang 14+ (LLVM 19 recommended), GCC 10+, or MSVC 19.30+.
- **Build system:** CMake **3.20+** and Ninja (or any other CMake generator).
- **Standard:** C++17.
- **Dependencies:** fetched automatically by CMake — [`nlohmann/json`](https://github.com/nlohmann/json)
  and [`Catch2` v3](https://github.com/catchorg/Catch2). Nothing needs to be pre-installed.

## Project layout

```
.
├── .github/workflows/ci.yml   # CI matrix (Ubuntu + Windows)
├── docs/Doxyfile.in           # Doxygen template
├── examples/                  # Runnable examples (cli_harness)
├── include/OpenNPC/           # Public headers (Types.h, NPCBrain.h)
├── src/                       # Library implementation
├── tests/                     # Catch2 test suite
├── .clang-format              # Code style
├── .clang-tidy                # Static-analysis rules
├── .clangd                    # Editor language-server config
├── CMakeLists.txt
└── LICENSE, README.md
```

## Development

```bash
# Build with static analysis enabled (clang-tidy runs on every compile)
cmake -S . -B build -G Ninja -DOPENNPC_ENABLE_CLANG_TIDY=ON

# Format the tree
clang-format -i $(git ls-files '*.h' '*.hpp' '*.cpp')

# Generate documentation (requires Doxygen)
cmake -DOpenNPC_ENABLE_DOCS=ON -S . -B build && cmake --build build --target docs
```

## Contributing

Contributions are welcome. Please open an issue to discuss substantial changes first, keep
the code formatted with `clang-format` and make sure `ctest` passes locally before opening a
pull request.

## License

Released under the [MIT License](LICENSE).
