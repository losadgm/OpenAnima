# Harnesses

Two clients are built from `examples/`. Both are examples of how to drive the library, and
both are meant to be read as much as run.

| Client | What it is | Where it stands |
| --- | --- | --- |
| `cliHarness` | The command line: one prompt at a time, or a session over stdin | in place |
| `tuiHarness` | The terminal UI: the conversation on screen, the answer arriving as it is written | planned, v0.9.0 |

## cliHarness

It reads the configuration from the environment, keeps one conversation across turns and
prints the answer as it arrives, piece by piece.

```bash
./build/examples/open_anima_cli "who are you?"   # one prompt, one answer, exit
./build/examples/open_anima_cli                  # a session, one turn per line
```

`quit` or `exit` ends a session, and so does the end of the input. The environment is the one
in [the configuration table](index.html#a-request-end-to-end), and `.env` is loaded on its own
from the working directory or from up to three of its parents, which is what makes the editor
buttons work with no wiring. The client is the reference for the streamed call: `ask()` with a
sink, and nothing else.

## tuiHarness

The console application, and the client where the library is meant to be *used* rather than
demonstrated: a conversation with an NPC, the answer arriving while the model writes it, and
the keys alive the whole time. It is also where the room of several agents will live (v0.6.0),
and the piece that has to look good enough to show the library off.

It is not built yet. What follows is the design, so that the shape is agreed before the code
exists.

### The problem

- The answer takes seconds. A client that waits for it cannot be interrupted, cannot be
  scrolled, and hides the fact that a model is working at all.
- What is on screen has to survive: the scrollback belongs to the user, and the application
  writes in the last lines only.
- It has to be verifiable. Logic that only exists inside a terminal cannot be tested, and this
  repository does not ship untested code.

### What it looks like

The reference is the OpenCode TUI, which solved the same problem for a coding agent: a
transcript that grows upwards, a prompt at the bottom, and a hint line that says what is
happening and what can be pressed.

```
  you     Do you have any iron daggers?
  Aldric  Three of them, and none of them cheap.
          The last one I sold went to a guard who
          never came back for it. Aye, that one
          cost him eleven silver.
          working 2.4s   esc to cancel

> what about the price?_
  qwen2.5:7b   /help for the keys   ctrl+d to leave
```

- The conversation *is* the scrollback: what leaves the screen stays in the history of the
  terminal, and the application never takes it over.
- The live region is redrawn in place while the answer arrives, and a finished answer is
  written once as plain lines, so it stays there untouched.
- Each role has its own colour and its own prefix, so a long session is readable at a glance.
- The hint line under the prompt carries the state: which model answered, how long the turn
  has been running, and the key that stops it.
- While a turn is in flight the prompt stays usable, the next question can be typed, and the
  keys that matter cancel or leave.

### Keys

| Key | What it does |
| --- | --- |
| Enter | Sends the draft |
| Ctrl+J, or a trailing `\` | Starts a new line instead of sending |
| Up, Down | Walks the prompts of this session |
| Esc, Ctrl+C | Cancels the turn in flight, keeping what it had produced |
| Ctrl+C with nothing in flight | Clears the draft, and leaves when it is already empty |
| Ctrl+D | Leaves |

### Commands

| Command | What it does |
| --- | --- |
| `/clear` | Empties the screen. The conversation is untouched |
| `/history clear` | Drops the conversation, keeping the persona and the connection |
| `/help` | Lists the keys and the commands |
| `/quit` | Leaves |

### How it is built

- **A terminal layer of its own**, with two implementations: raw mode and ANSI sequences on
  Unix (termios), and the console API with virtual terminal sequences on Windows. Windows is a
  target from the start rather than a port to attempt later.
- **The draw loop is driven by the events of a turn.** `askAsync()` and `poll()` already hand
  the text over as it arrives, so the screen changes when something happens and the loop never
  blocks. `cancel()` is what the interrupting keys call.
- **The screen logic knows nothing about terminals.** What to draw, where the cursor goes and
  what a key means are pieces that take a width and a string and return lines, which is what
  makes them testable without a terminal.
- **Two clients, one configuration**: the same environment, the same `.env` loader and the
  same provider as the command line.

### How it is checked

- The pieces: layout, key decoding and the transcript model, with the terminal replaced by a
  fake.
- The client itself: a pseudo-terminal in the suite that sends keys and reads what comes out.
  PTY on Unix and ConPTY on Windows, so the same test runs on all three platforms.

### The plan

1. Two prototypes, one over FTXUI and one written by hand, in a branch of their own, with the
   minimum needed to compare them: the prompt, the answer growing, and the key that cancels.
   They are thrown away once one of them wins.
2. The skeleton of the real client around the winner, with the terminal layer abstracted and
   both of its implementations.
3. The loop: input, live region, cancellation, history and commands.
4. The tests above, and these pages brought up to date.
