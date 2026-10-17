# Harnesses

Two clients are built from `examples/`. Both are examples of how to drive the library, and
both are meant to be read as much as run.

| Client | What it is | Where it stands |
| --- | --- | --- |
| `cliHarness` | The command line: options in, one prompt or a session out | in place |
| `tuiHarness` | The terminal UI: the conversation on screen, the answer arriving as it is written | built |

## cliHarness

The command-line client. Every setting is an option, the npcs are created from the command
line or from a command, and the turn is printed once it is over, in the order the reply
contract defines it.

```bash
./build/examples/open_anima_cli --help                    # the manual
./build/examples/open_anima_cli --persona "a blacksmith" "who are you?"
./build/examples/open_anima_cli --persona "a blacksmith"  # a session over stdin
```

- An option wins over the variable it reads, `OPENANIMA_CLI_<NAME>`, and the variable wins
  over `.env`, which is only a way of setting variables. `--base-url` and `--model` are the
  two that have to come from somewhere; with both, the client runs with no `.env` at all.
- There is no npc until one is asked for: `--persona` creates it at startup, `/create` in a
  session. A prompt with nobody to answer it says how to make one instead of asking a model
  without a role.
- `--provider` picks the provider, and `rest` is the only one that exists today.
- `--raw`, or `/raw` in a session, prints the reply document and nothing else: the three
  variables as the model wrote them, which is what to look at when a turn comes back wrong.
- The session speaks the vocabulary of the console client: `/create`, `/enter`, `/npcs`,
  `/leave`, `/clear`, `/raw`, `/help` and `/quit`. Given an argument it does not take, each
  answers with its usage.
- `ctrl-c` stops the turn in flight and keeps what it had said; twice, and it leaves. `ctrl-d`
  leaves too.
- When stdin is not a terminal there is no banner and no prompt, so a session can be piped.

The settings are read by `examples/common/apiSettings`, shared with the terminal client under
its own prefix, and the loader for `.env` is `examples/common/dotenv`. The client is the
reference for the asynchronous turn: `askTurnAsync()` with `poll()`, `cancel()` wired to
ctrl-c, and the whole document available when the turn is over. The dialogue is written once
the turn is finished rather than while it arrives, so that the reasoning keeps its place above
the words it leads to; nothing else is lost, because the answer is kept whole either way.

## tuiHarness

The console application, and the client where the library is meant to be *used* rather than
demonstrated: a conversation with an NPC, the answer arriving while the model writes it, and
the keys alive the whole time. It is also where the room of several agents will live (v0.7.0),
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

  +---------------------------------------------------------------+
  | > /c_                                                         |
  +---------------------------------------------------------------+
        mock   ask something   /help for keys
```

The commands are listed while one is being typed, up to six of them, with the selected one in
the accent colour. When there are more than fit, the panel shows the ones around the
selection, so a command reached with the arrows is never off the panel.

A blank row separates every turn from the one before it, so a conversation reads as a question
and the answer it got. The answer itself is one block: the name of the NPC sits on its first
line, over the thought that opens it, and the dialogue and the actions follow without
repeating the name or opening a gap.

- The client owns the screen: it runs in the alternate screen, paints the whole window on a
  change, and hands the terminal back on the way out.
- The conversation lives in a viewport of its own. Page up and page down move it, the wheel
  moves it a row at a notch, and the prompt stays where it is, pinned under the transcript.
  A one-column bar on the right shows how much of the conversation is on screen and where in
  it the viewport sits, and it can be dragged to move the view.
- The prompt is the fixed part: the draft is drawn inside a frame of its own, with the command
  in its own colour while one is being typed, and the caret never leaves it. The frame runs
  along the inside edge of the panel with the one-eighth blocks and the legacy computing corners
  that join two of them, an eighth on every side: one pixel across and two down, because a cell
  is twice as tall as it is wide, so the colour reaches the edge of the box and none of it shows
  outside the strokes. The corners need a font that covers U+1FB00 to U+1FBFF, which DejaVu Sans
  Mono does not. The band inside is the line itself, with a column of it on each side, so the
  text never sits against the frame. While the line is empty the frame says what can be done
  from there, which is not the same in the lobby as in a conversation, and the words go as soon
  as there is something to say.
- The commands that complete what is being typed float over the last rows of the conversation,
  on a lighter panel of their own. The panel takes thirty per cent of the width and is anchored
  to the left edge, so the rest of those rows still shows what was said, and it takes no rows
  away from the viewport. The selected one is in the accent colour; the arrows walk them and
  stop at the first and the last one instead of coming round, tab writes the selected one, and
  a click writes the one under the pointer. When more commands match than the panel has rows,
  its right edge carries a bar of its own, the same as the one of the conversation, which can be
  dragged to walk the list.
- Each role has its own colour: the player in muted gray, the NPC in the accent colour, and
  the thought quietly under the same name, so it reads as the NPC's and not as the client's.
- A note is what the client says on its own, and it belongs to no speaker: it starts at the
  left edge of the transcript, painted quietly, while what an NPC says and does stays lined up
  under its name.
- The look borrows the OpenCode theme: a purple accent for the prompt marker and the spinner,
  a warm peach for the persona, muted gray for the quiet text, and the same braille spinner.
- The status line sits under the frame and carries the state: which model answered, how long
  the turn has been running, and the key that stops it.

### Keys

| Key | What it does |
| --- | --- |
| Enter | Sends the draft |
| Ctrl+Enter, Ctrl+J, or a trailing `\` | Starts a new line instead of sending |
| Tab | Completes the selected command |
| Up, Down | Walks the command hints while they are showing, stopping at the ends, and the prompts of this scope otherwise |
| Ctrl+A, Ctrl+E, Ctrl+B, Ctrl+F, Left, Right | Moves the cursor in the draft |
| Ctrl+Left, Ctrl+Right | Moves a word at a time |
| Backspace, Delete | Deletes the character before or after the cursor |
| Ctrl+Backspace, Ctrl+Delete | Deletes a word at a time, and the gap around it |
| Ctrl+P, Ctrl+N | Walks the prompts of this scope |
| Page up, Page down | Scrolls the conversation |
| Wheel | Scrolls whatever the pointer is over: the conversation, or the hints |
| Drag a scrollbar | Scrolls to where the thumb is put, following the button |
| Hover a hint or a scrollbar | Lights it up: the hint goes lilac, bold and underlined, the thumb of the bar goes lilac and bold |
| Click a hint | Writes it into the draft, the same as tab with the selected one |
| Esc | Cancels the turn in flight, keeping what it had produced |
| Drag over the conversation | Picks out those cells, painted reversed as the pointer moves |
| Press and release on one cell | Picks out that one character |
| Double click | Picks out the word under the pointer |
| Triple click | Picks out the whole row |
| Ctrl+C | Copies what is picked out and drops the picking. It never leaves the client, which is what keeps a reach for the copy key from ending the session |
| Ctrl+V | Types what was copied last |
| Ctrl+D | Leaves |

Control and backspace travel as the same byte as Ctrl+H on the terminals that do not report the
chord itself, which is nearly all of them, so the client reads that byte as the word delete: it
is what pressing the two keys produces there, and Ctrl+H has nothing else to do in a line being
typed. The same goes for the other spellings the chords turn into: Ctrl+W, alt and backspace,
and the modifier forms `27;5;3~` and `27;5;127~`. Control and delete comes in as `3;5~` from a
terminal, and as alt and `d` from an editor that takes the chord before the terminal sees it,
which is what VS Code does. An editor that sends the key through the kitty keyboard protocol
reports the code point instead, so code point 3 reaches the word delete too and code point 127
the word backspace.

A tab is never written into the draft. It counts as one column to the client and as up to
eight to the terminal, and the two disagreeing is what tore the row apart.

The picking out follows the button for as long as it is held: the cells are painted as the
pointer passes over them, the paint grows and shrinks with it, and the last one stays painted
when the button comes back up. A double click takes the word under the pointer and a triple
click takes the whole row, the way a terminal does it; a cell that is not part of a word is a
word of its own. Picking is only over what was said: the box of the prompt, its padding, the
marker beside it and the scrollbar on the right are the client's own drawing, so a drag across
them paints the text and leaves them as they were, and a copy of that same selection holds the
words without the box and comes out flush with the left edge.
Any other key drops the picking, so a selection never sits over text that has moved under it.
While the client runs the mouse is the client's, which is the price of the wheel scrolling the
conversation; holding shift asks the terminal for its own selection back in most of them. The
right button is left to the terminal: the client never uses it, so the menu of the editor or of
the emulator opens with the copy and paste it offers.

The pointer also lights up what it is over. A command hint under it turns lilac, bold and
underlined, and a click on it writes it into the draft, which is what tab does with the one that
was selected. A hint that is not under the pointer is quiet unless it is the selected one, which
keeps the accent colour. The wheel and the page keys are the wheel of whatever is under the
pointer: over the panel they walk the hints, stopping at the ends, and anywhere else they scroll
the conversation. A
scrollbar under it gives the pointer the thumb it would drag: the thumb turns lilac and bold,
and the button drags it, which scrolls the conversation to the row it is left on, or walks the
hints when it is the bar of the panel. A press beside a thumb brings the thumb under the pointer
first, and a drag that goes past either end stops there.

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
| `/raw [on|off]` | Shows every turn as the JSON the model wrote, instead of the dialogue |
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
- **Two clients, one configuration**: the same settings reader, the same `.env` loader and the
  same provider as the command line, read from its own prefix, `OPENANIMA_TUI_`. The two can
  point at different models from the same shell.

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
