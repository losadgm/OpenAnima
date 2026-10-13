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
buttons work with no wiring. The loader is `examples/common/dotenv`, shared with the terminal
client. The client is the reference for the streamed call: `ask()` with a sink, and nothing
else.

## tuiHarness

The console application, and the client where the library is meant to be *used* rather than
demonstrated: a conversation with an NPC, the answer arriving while the model writes it, and
the keys alive the whole time. It is also where the room of several agents will live (v0.6.0),
and the piece that has to look good enough to show the library off.

It is built, and it is provisional: a harness is where the shape of the library is tried out,
so it moves every time the API moves, and what is written here describes today rather than a
promise. What follows is the shape that was agreed before the code existed, and what the code
now does.

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

  /create
  /enter
  /npcs

        mock   ask something   /help for keys

❱ /c_
```

The commands are listed while one is being typed, up to six of them, with the selected one in
the accent colour, and the list is cut rather than pushed around when the window is short.

A blank row separates every message from the one before it, so a conversation reads as turns
instead of as one block of text.

- The client owns the screen: it runs in the alternate screen, paints the whole window on a
  change, and hands the terminal back on the way out.
- The conversation lives in a viewport of its own. Page up and page down move it, and the
  prompt stays where it is, pinned under the transcript.
- The prompt is the fixed part: the draft is drawn on a panel of its own, with the command in
  its own colour while one is being typed, and the caret never leaves it.
- The commands that complete what is being typed float above the prompt on a lighter panel,
  with the selected one in the accent colour. The arrows walk them, and tab takes the one that
  is selected.
- Each role has its own colour and its own prefix, so a long session is readable at a glance.
- The look borrows the OpenCode theme: a purple accent for the prompt marker and the spinner,
  a warm peach for the persona, muted gray for the quiet text, and the same braille spinner.
- The status line carries the state: which model answered, how long the turn has been running,
  and the key that stops it.

### Keys

| Key | What it does |
| --- | --- |
| Enter | Sends the draft |
| Ctrl+Enter, Ctrl+J, or a trailing `\` | Starts a new line instead of sending |
| Tab | Completes the selected command |
| Up, Down | Walks the command hints while they are showing, and the prompts of this scope otherwise |
| Ctrl+A, Ctrl+E, Ctrl+B, Ctrl+F, Left, Right | Moves the cursor in the draft |
| Ctrl+P, Ctrl+N | Walks the prompts of this scope |
| Page up, Page down | Scrolls the conversation |
| Wheel | Scrolls the conversation, three rows a notch |
| Esc, Ctrl+C | Cancels the turn in flight, keeping what it had produced |
| Drag over the conversation | Picks out those cells, painted reversed as the pointer moves |
| Ctrl+C with something picked out | Copies it, and drops the picking |
| Ctrl+C with nothing in flight or picked out | Clears the draft, and leaves when it is already empty |
| Ctrl+V | Types what was copied last |
| Ctrl+D | Leaves |

A tab is never written into the draft. It counts as one column to the client and as up to
eight to the terminal, and the two disagreeing is what tore the row apart.

The picking out follows the button for as long as it is held: the cells are painted as the
pointer passes over them, the paint grows and shrinks with it, and the last one stays painted
when the button comes back up. Everything the conversation holds can be picked out, the prompt,
the panels and the hints included, because what is read back is what was painted. While the
client runs the mouse is the client's, which is the price of the wheel scrolling the
conversation; holding shift asks the terminal for its own selection back in most of them.

Anything a keyboard can send reaches the draft as bytes, so an enye, an accented vowel or a
combining mark typed after a letter lands in the same cell as the letter it belongs to. Copying goes out as the terminal's own clipboard sequence and lives in
the client too, so a paste works in a terminal that keeps the clipboard to itself.

### The world

The client starts with no NPC at all, and the conversation is with whoever it was told to
talk to:

| Command | What it does |
| --- | --- |
| `/create <description>` | Makes an NPC from the description and talks to it |
| `/enter <id>` | Talks to another NPC, with the conversation where it was left |
| `/npcs` | Lists the NPCs, and which one is being talked to |
| `/leave` | Leaves the conversation, keeping it |
| `/clear` | Drops the active conversation, keeping the persona |
| `/help` | Lists the keys and the commands |
| `/quit` | Leaves |

An NPC has no name yet, only the number it was given when it was made, so it is shown as
`npc 1`, `npc 2`. The persona is the description it was created from. Any number of NPCs can
be made and each one keeps its conversation, reachable by id. NPCs cannot talk to each other:
that needs the library to carry a conversation between several of them, which is not there yet.

Each scope has its own prompt history: inside an NPC the arrows walk what was typed to that
NPC, and in the lobby what was typed outside. A command that would leave a turn behind waits
for the answer, and says so.

### How it is built

- **A terminal layer of its own**, with two implementations: raw mode and ANSI sequences on
  Unix (termios), and the console API with virtual terminal sequences on Windows. Windows is a
  target from the start rather than a port to attempt later. It is `terminal/terminal.h` with
  `terminal/terminalUnix.cpp` and `terminal/terminalWindows.cpp`, and it is where keys, the
  size of the window, the alternate screen and writing to the terminal come from. Writing never
  waits for the terminal: what does not fit is kept and sent on the next turn of the loop, so a
  terminal that has stopped reading cannot freeze the client and leave the keyboard unread.
- **The rest is a functional core and an imperative shell.** Under `app/` the model, the
  commands and the keys are pure: `update(model, key)` returns what to do as a list of
  effects, and `applyEvent(model, event)` folds what came back from the provider into the same
  model. Under `screen/` the rendering is pure too: cells into a canvas, and the canvas into
  rows. `main.cpp` is the shell, and it is the only place that knows about files, sockets and
  clocks: it runs the effects against the brains, drains the events, and writes a frame when
  something changed and the one before it has arrived.
- **The screen is a grid of cells, not a string of escapes.** A style is a value (a colour, a
  background, the attributes) and a cell holds a character and the style it is painted with,
  so two frames can be compared before anything is written. Text is placed by columns: a wide
  character takes two cells and is never split, a combining mark joins the letter before it,
  and a row that would not fit is cut rather than left to wrap on its own.
- **A frame costs what changed in it.** The writer keeps the frame the terminal is showing,
  compares it with the next one and speaks only about the difference: a row that did not change
  is not written, a cell that did not change is not written, the cursor is not moved to where
  it already is, and a style is only sent when it differs from the one in force. A screen that
  looks the same as the last one costs nothing, and an answer arriving word by word costs a
  handful of bytes per word instead of the whole screen: measured at 56 bytes a frame against
  the 3360 a full repaint would take.
- **The draw loop is driven by the events of a turn.** `askAsync()` and `poll()` already hand
  the text over as it arrives, so the screen changes when something happens and the loop never
  blocks. `cancel()` is what the interrupting keys ask for, through an effect. While a frame is
  still on its way, the model keeps advancing and no new frame is composed: the next one drawn
  is the latest state, not the one after the one nobody has seen.
- **The world lives in the shell.** Each NPC is a `NPCBrain` with a persona and its own
  transcript; the model holds the ids, the transcripts and which one is being talked to. The
  library does not need to know that several NPCs exist until they have to speak to each
  other.
- **Two clients, one configuration**: the same environment, the same `.env` loader and the
  same provider as the command line.

### Packets

| Where | What is in it |
| --- | --- |
| `app/` | The model, the commands and the keys: what a key does, and what to ask the shell for |
| `screen/` | The canvas, the cells, the colours, the text measurement and the frame writer |
| `terminal/` | Raw mode, key decoding, the size of the window and writing without waiting |
| `main.cpp` | The shell: brains, effects, events, and the loop that decides when to draw |

### How it is checked

- The pieces: `tests/tui/` covers the text (columns, wrapping, cutting), the canvas (clipping,
  wide characters, combining marks), the frame writer (what it sends, and whether a terminal
  that applies it ends up showing the frame it came from), the commands (matching and
  completing), the model (every key, the world, the events) and the screen (panels, hints,
  viewport, caret, and what the mouse picked out). No terminal is involved.
- The client itself: driven over a pseudo-terminal, sending keys and reading what comes back,
  with a throwaway server that streams the answer word by word. The keys, the wheel, the
  accented characters, the picking out and the clipboard are checked that way, and so is the
  rule that nothing but whole characters ever leaves the client towards the terminal. PTY on
  Unix and ConPTY on Windows is where this is going, so the same test runs on all three
  platforms.

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

Steps 1 to 3 are done, and the piece tests of step 4 are in the suite. The enhanced keyboard
is in, the wheel is in, and so is picking text out of the conversation with the mouse and
handing it to the clipboard. What is left of step 4 is the in-suite client test over a
pseudo-terminal, so the client itself is exercised by the same command that runs the rest.

What comes next, in the order it will be felt:

1. A measured layout: today the rows of the screen are placed by arithmetic on the height and
   the width, which is what makes a narrow window and a resize the places where the client can
   still be surprised. The screen is going to describe itself as a tree of boxes with sizes to
   honour, measure it and then paint it, so that what is asked for and what is shown cannot
   drift apart.
