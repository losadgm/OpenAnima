---
name: 'Code generation'
description: 'Use when writing, changing or reviewing C++ code in this project: where a file goes, how it is named, how it is documented, when to assert and when to throw, and the checks a change passes before it is called done.'
applyTo: '**/*.h, **/*.hpp, **/*.cpp, **/*.cxx'
---

# Generating code

## Start from the neighbours

Match the file next to yours: same layout, same naming, same kind of comment. The public
API is small on purpose, so a new header, type or parameter in `include/` has to earn its
place.

## Where things go

- Public headers in `include/OpenAnima/`, one folder per package: `Core`, `Http`,
  `Provider`, with the error taxonomy at the root.
- Implementation in `src/OpenAnima/`, mirroring the header path exactly:
  `OpenAnima/Core/NPCBrain.h` is implemented by `OpenAnima/Core/NPCBrain.cpp`.
- A header that is an implementation detail lives in `src/` with the same shape and is not
  installed, the way `src/OpenAnima/Provider/SseReader.h` is.

## Naming

- Types `CamelCase`; functions, methods and variables `camelBack`; private members `m_`.
  The linter checks all four, so a mismatch is a finding, not a preference.
- C++17. No owning raw pointers, no `new`, and no exception leaving a destructor.

## Documentation

- English, plain ASCII: no typographic characters, no emoji.
- Doxygen sits next to the declaration, and that is the only place a thing is documented.
- Every public function documents its parameters and its return value. A trivial getter
  may skip it.
- Say what the thing is for. How it works inside belongs in the code.

## Preconditions and failures

- An `assert` marks a broken precondition: null, empty, malformed. It is a programming
  error, not something the caller can handle.
- A rule of the domain is raised as an exception, from `OpenAnimaError` or one of its
  subclasses.
- Absence is not `null`: return `std::optional`, or an empty value together with the
  documentation saying so.

## Tests

- Catch2 under `tests/`, mirroring the package. They never touch the network:
  `FakeHttpTransport` and `FakeProvider` exist for that.
- Wrap the cases in `NOLINTBEGIN(...)`/`NOLINTEND(...)` for the checks the assertions trip,
  and close the block. An unmatched `NOLINTBEGIN` is an error to clang-tidy, and it stops
  suppressing everything below it without saying so.
- A case name never starts with a slash, however much it names a command. CTest on Windows
  reads it as one of its own options: the case fails in milliseconds, prints nothing, and the
  log only shows `***Failed`. Write `the /create command makes the npc` where `/create makes
  the npc` would read the same.
- Test the ugly case rather than the happy one: half a message, an empty field, a field
  nobody knows.

## Before it is done

1. `cmake --build build` with no diagnostics. Every target compiles with
   `-Wall -Wextra -Wpedantic`, examples and tests included.
2. `ctest --test-dir build --output-on-failure` green.
3. `clang-format --dry-run --Werror <files>` silent. The style is in `.clang-format`.
4. The clang-tidy review below, with nothing left open.
5. No unused includes, by the include cleaner below.
6. The API reference builds, which is the `docs` job of the pipeline and not something the
   compiler can tell you about. See the last section.

Once it passes, what reaches GitHub with it, and how the roadmap and the issues are kept in
step, is in `github.instructions.md`.

## The clang-tidy review

Compiling is not the bar. The editor runs clangd over `.clang-tidy` and
`build/compile_commands.json`, which is exactly what the command line tool reads, so this
review is the one that decides whether the next reader meets anything in Problems.

```bash
clang-tidy --use-color=0 -p build <the translation units the change touches> 1>/tmp/tidy.txt 2>/tmp/tidy.err
grep -cE "^[^ ]+:[0-9]+:[0-9]+: (warning|error):" /tmp/tidy.txt
grep -E "^[^ ]+:[0-9]+:[0-9]+: (warning|error):" /tmp/tidy.txt | grep -v /_deps/
tail -2 /tmp/tidy.err
```

- **`--use-color=0` is not optional.** `.clang-tidy` sets `UseColor: true`, so a plain run
  wraps every line in escape sequences: nothing starts with a path any more, a filter
  matching the shape of a diagnostic finds nothing, and a run with thirty findings reads
  exactly like a clean one. The option takes a boolean, so it is `0` or `1`.
- **The two streams carry different things.** The findings go to stdout. The progress
  (`[1/3] Processing file ...`), the summary, and everything that went wrong go to stderr:
  `Found compiler error(s)` and `Error while processing` are there, and so is the closing
  `Suppressed N warnings (...)`, which is how a run that finished is told from one that
  stopped. Capture both, and read both. Sending stderr to `/dev/null` hides a file that was
  never analysed; reading only stderr hides every finding.
- **The exit code is not the answer either.** Warnings do not fail the run: a pass with
  hundreds of findings leaves ours at 0, and so does an empty one. The count and the
  summary are the answer.
- **Count before filtering, and open the log.** `grep -cE` says whether there is anything to
  read at all. A filter that returns nothing is a claim about the filter until the file has
  been looked at.
- Run it over every translation unit the change touches, not only the new file. A finding
  normally lands in a header, or in whoever includes it.
- Keep the lines pointing into `src/`, `include/`, `tests/` or `examples/`. The rest is
  nlohmann, Catch2 and the platform headers, and it is most of the output: a review with
  nothing to say prints thousands of lines about the dependencies.
- A line reading `N warnings generated` is not the number of findings: it counts the whole
  run, dependencies included. A line of garbage, or one of the echoed source lines, is not
  a finding either.
- Search the output for `error`. clang-tidy calls errors things the compiler accepts, an
  unmatched `NOLINTBEGIN` among them, and when it finds one it stops analysing that file:
  `Found compiler error(s)` means the review did not finish.
- Every finding that lands in our files is either fixed or answered with a reason. Not
  having noticed it is not a reason.
- Two findings come back often enough to write down:
  - `bugprone-unchecked-optional-access` after `REQUIRE(value.has_value())`: the analyzer
    cannot see through the macro, so it reads the `value()` that follows as unchecked. Take
    it into a reference under `// NOLINTNEXTLINE(bugprone-unchecked-optional-access)`, with
    a comment saying that the assertion above is the check.
  - `NOLINTBEGIN(...)` fits on one line or it does not work. A check list that goes past
    100 columns is reflowed by clang-format, clang-tidy parses the marker per line, and a
    list split in two comes back as `unmatched NOLINTBEGIN`: an error, and the suppression
    for everything below it is gone. When the list is long, use per-line
    `// NOLINTNEXTLINE(check)` instead of a block.

## Unused includes

A file includes what it uses, and nothing else. The editor says so because `.clangd` sets
`Diagnostics.UnusedIncludes: Strict`; the command line says the same thing through the
include cleaner of clang-tidy:

```bash
for f in <files>; do
  clang-tidy -p build --use-color=0 --checks='-*,misc-include-cleaner' "$f" 2>/dev/null \
    | grep -E "warning: .*(is not used directly|no header providing)" | sed "s|^|$f :: |"
done
```

- A header reported as `included header X is not used directly` goes, together with any
  `using` that only lived on it. Include what the file uses, not what its neighbour happened
  to include.
- The other half of the cleaner, `no header providing ... is directly included`, is not this
  step. Most of it is a count, a string or an enum that a header already included for another
  reason happens to provide; adding one include per line grows the list without changing what
  the file depends on. Add the direct include when the file starts to rely on the type, and
  leave the rest.
- A file that gains or loses an include still has to compile and pass the rest of the list,
  so run it before the build rather than after.

## Keeping clangd honest

- `.clangd` reads its flags from `build/compile_commands.json`, which exists because the
  build exports it. After adding, moving or deleting a file, configure again before
  believing the editor: a file that is not in the database gets guessed flags in Problems,
  and `clang-tidy -p build <file>` falls back to a default command with a warning instead of
  the real one.
- When the editor and the command line disagree, the database is the stale one. Point
  clangd at the build tree the change was compiled in, not at another one.
- `.clangd` adds checks of its own on top of `.clang-tidy`, and never removes one the
  review depends on. Passing the command line review is the stronger of the two.

## The API reference is a check too

The `docs` job builds the reference over `include/` with the Doxygen that ships in Ubuntu
(1.9.8 today) and rewrites `WARN_AS_ERROR` to `YES` first, so a missing `@param`, a missing
`@return` or a broken cross reference fails the pull request after the build and the tests
have already gone green. Compiling says nothing about it.

Where it bites is a member that is pure virtual in a base class and overloaded in the class
being documented, which is both interfaces of this library:

- `@copydoc Base::name` needs a name that appears once in `Base`. With more than one, it
  resolves to none of them and the override comes out with its parameters undocumented.
  Spelling the arguments, `@copydoc Base::name(const A&, const B&)`, is not resolved either.
  Write the block out and give the override its own `@param`s.
- `using Base::name;`, the way a derived class keeps a convenience overload reachable,
  resolves to the abstract member the name was brought in from, and Doxygen then asks the
  class to document parameters it never declares. Declaring the overload and forwarding to
  the base reads the same and documents cleanly.

To run the job's check before pushing, use the same version the runner installs:

```bash
curl -sL -o /tmp/doxygen.tar.gz \
  https://github.com/doxygen/doxygen/releases/download/Release_1_9_8/doxygen-1.9.8.linux.bin.tar.gz
tar xzf /tmp/doxygen.tar.gz -C /tmp
PATH=/tmp/doxygen-1.9.8/bin:$PATH cmake -S . -B build-docs -G Ninja \
  -DBUILD_TESTING=OFF -DOPENANIMA_BUILD_EXAMPLES=OFF -DOPENANIMA_ENABLE_DOCS=ON
sed -i 's/^WARN_AS_ERROR *=.*/WARN_AS_ERROR = YES/' build-docs/Doxyfile
PATH=/tmp/doxygen-1.9.8/bin:$PATH cmake --build build-docs --target docs
```

A newer local Doxygen is not the check: the warnings differ between releases, and a version
that is happy says nothing about the runner. When the job fails, the log names the file and
the line, and the run above reproduces it in seconds.
