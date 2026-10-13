# Harnesses

Two clients are built from `examples/`. Both are examples of how to drive the library, and
both are meant to be read as much as run.

| Client | What it is | Where it stands |
| --- | --- | --- |
| `cliHarness` | The command line: one prompt at a time, or a session over stdin | in place |
| `tuiHarness` | The terminal UI: the conversation on screen, the answer arriving as it is written | built |

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

It is built. What follows is the shape that was agreed before the code existed, and what the
code now does.

### The problem

- The answer takes seconds. A client that waits for it cannot be interrupted, cannot be
  scrolled, and hides the fact that a model is working at all.
- The prompt has to stay put while the conversation moves under it, and the conversation has to
  be readable back, however long it gets.
- It has to be verifiable. Logic that only exists inside a terminal cannot be tested, and this
  repository does not ship untested code.

### What it looks like

The reference is the OpenCode TUI, which solved the same problem for a coding agent: a
transcript with a scroll of its own, a prompt pinned to the bottom, and a hint line that says
what is happening and what can be pressed.

```
  you     Do you have any iron daggers?
  npc 1   Three of them, and none of them cheap.
          The last one I sold went to a guard who
          never came back for it. Aye, that one
          cost him eleven silver.

  ⠹ working 2.4s   esc to cancel

❯ what about the price?_
  mock   ask something   /help for keys
```

- The client owns the screen: it runs in the alternate screen, paints the whole window on a
  change, and hands the terminal back on the way out.
- The conversation lives in a viewport of its own. Page up and page down move it, and the
  prompt stays where it is, pinned under the transcript.
- The prompt is the fixed part: the draft is drawn there, with the command in its own colour
  while one is being typed, and the commands that complete it floating above.
- Each role has its own colour and its own prefix, so a long session is readable at a glance.
- The look borrows the OpenCode theme: a purple accent for the prompt marker and the spinner,
  a warm peach for the persona, muted gray for the quiet text, and the same braille spinner.
- The hint line carries the state: which model answered, how long the turn has been running,
  and the key that stops it.

### Keys

| Key | What it does |
| --- | --- |
| Enter | Sends the draft |
| Ctrl+J, or a trailing `\` | Starts a new line instead of sending |
| Tab | Completes the command being typed |
| Ctrl+A, Ctrl+E, Ctrl+B, Ctrl+F, Left, Right | Moves the cursor in the draft |
| Up, Down | Walks the prompts of this session |
| Page up, Page down | Scrolls the conversation |
| Esc, Ctrl+C | Cancels the turn in flight, keeping what it had produced |
| Ctrl+C with nothing in flight | Clears the draft, and leaves when it is already empty |
| Ctrl+D | Leaves |

### The world

The client starts with no NPC at all, and the conversation is with whoever it was told to
talk to:

| Command | What it does |
| --- | --- |
| `/create <description>` | Makes an NPC from the description and talks to it |
| `/enter <id>` | Talks to another NPC, with the conversation where it was left |
| `/npcs` | Lists the NPCs, and which one is being talked to |
| `/leave` | Leaves the conversation, keeping it |
| `/history clear` | Drops the active conversation, keeping the persona |
| `/help` | Lists the keys and the commands |
| `/quit` | Leaves |

An NPC has no name yet, only the number it was given when it was made, so it is shown as
`npc 1`, `npc 2`. The persona is the description it was created from. NPCs cannot talk to
each other: that needs the library to carry a conversation between several of them, which is
not there yet.

### How it is built

- **A terminal layer of its own**, with two implementations: raw mode and ANSI sequences on
  Unix (termios), and the console API with virtual terminal sequences on Windows. Windows is a
  target from the start rather than a port to attempt later. It is `terminal.h` with
  `terminalUnix.cpp` and `terminalWindows.cpp`, and it is where the alternate screen and the
  size of the window come from.
- **The draw loop is driven by the events of a turn.** `askAsync()` and `poll()` already hand
  the text over as it arrives, so the screen changes when something happens and the loop never
  blocks. `cancel()` is what the interrupting keys call.
- **The screen logic knows nothing about terminals.** What to draw, where the caret goes, what
  a key means and which commands complete what is being typed are pieces that take a width, a
  height and a string and return rows, which is what makes them testable without a terminal.
  It is `screen.h`.
- **The world lives in the client.** Each NPC is a `NPCBrain` with a persona and its own
  transcript; the conversation being shown is swapped in and out on `/enter` and `/leave`. The
  library does not need to know that several NPCs exist until they have to speak to each
  other.
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
   They are thrown away once one of them wins. The hand-written one won: FTXUI was dropped
   because its inline renderer emits cursor moves with a negative parameter (`\x1b[-1B`), which
   both the terminal in the editor and a strict emulator misrender.
2. The skeleton of the real client around the winner, with the terminal layer abstracted and
   both of its implementations.
3. The loop, the world of NPCs, the scroll of the transcript and the command palette.
4. The tests above, and these pages brought up to date.

Steps 1 to 3 are done, and the piece tests of step 4 are in the suite. What is left is the
in-suite client test over a pseudo-terminal, so the client itself is exercised by the same
command that runs the rest.
