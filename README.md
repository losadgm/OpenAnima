# OpenAnima

[![CI](https://github.com/losadgm/OpenAnima/actions/workflows/ci.yml/badge.svg)](https://github.com/losadgm/OpenAnima/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://github.com/losadgm/OpenAnima)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

C++17 middleware for conversational NPCs: agents that talk to the player, react to the world
around them and talk to each other.

A chat client sends a prompt and prints what comes back. An NPC is not that. It plays a persona,
remembers what happened, and has to stay coherent while the player, the environment and the
other agents keep talking to it. OpenAnima is the layer in between: it holds the state of an
agent, decides what the model needs to see next, and turns the answer into something the host
application can act on.

The conversation is one of three channels an NPC uses. All three are part of the design:

| Channel | What it carries | Where it stands |
| --- | --- | --- |
| The player | dialogue turns, with a persona and a history behind them | `Core::NPCBrain`, in place |
| The world | events the agent perceives and reacts to | planned, v0.4.0 |
| Other NPCs | shared rooms, and a ledger of who said what | planned, v0.6.0 |

## What this is not

- Not an inference engine. It calls a provider over HTTP; running a model in-process is a later
  milestone. The model itself is never part of the library.
- Not a game engine or a world simulator. The host application owns the world. The library asks
  for what an agent should know and hands back what that agent decided.
- Not a wrapper around one vendor. `Provider::RestProvider` implements the chat-completions
  contract, which OpenAI, Ollama, llama.cpp and vLLM all speak; the agent state above it does not
  care which one answered.

## Status

Early scaffolding (v0.1.0). The player channel works end to end, which is why the public API is
deliberately small. The world and the multi-agent channels are on the roadmap.

- Documentation: https://losadgm.github.io/OpenAnima/
- API reference: https://losadgm.github.io/OpenAnima/api/
- Roadmap: https://github.com/losadgm/OpenAnima/blob/main/TODO.md
