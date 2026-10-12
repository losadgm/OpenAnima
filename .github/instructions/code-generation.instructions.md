---
name: 'Code generation'
description: 'Use when writing, changing or reviewing C++ code in this project: where a file goes, how it is named, how it is documented, when to assert and when to throw, and the checks a change passes before it is called done.'
applyTo: ["**/*.h", "**/*.hpp", "**/*.cpp", "**/*.cxx"]
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
- Test the ugly case rather than the happy one: half a message, an empty field, a field
  nobody knows.

## Before it is done

1. `cmake --build build` with no diagnostics. Every target compiles with
   `-Wall -Wextra -Wpedantic`, examples and tests included.
2. `ctest --test-dir build --output-on-failure` green.
3. `clang-format --dry-run --Werror <files>` silent. The style is in `.clang-format`.
4. The clang-tidy review below, with nothing left open.

## The clang-tidy review

Compiling is not the bar. The editor runs clangd with the checks in `.clang-tidy`, so
whatever this review leaves behind is what the next reader meets in Problems.

```bash
clang-tidy -p build <the translation units the change touches> 1>/tmp/tidy.txt
grep -E "^/[^ ]*:[0-9]+:[0-9]+: (warning|error):" /tmp/tidy.txt | grep -v /_deps/
```

- Run it over every translation unit the change touches, not only the new file. A finding
  normally lands in a header, or in whoever includes it.
- **The diagnostics go to stdout.** Capturing stderr reads an empty file, and an empty file
  looks exactly like a clean run.
- Match the shape of a diagnostic, as above, rather than the word `warning`. Under every
  finding clang-tidy echoes the offending source line, and one of those lines can carry the
  word on its own: filtering by path alone then reports findings that are not there.
- Keep the lines pointing into `src/`, `include/`, `tests/` or `examples/`. The rest is
  nlohmann, Catch2 and the platform headers.
- A line reading `N warnings generated` is not the number of findings: it counts the whole
  run, dependencies included. The findings are the `path:line:col: warning: [check]` lines.
- Search the output for `error`. clang-tidy calls errors things the compiler accepts, an
  unmatched `NOLINTBEGIN` among them, and when it finds one it stops analysing that file:
  `Found compiler error(s)` means the review did not finish.
- Every finding that lands in our files is either fixed or answered with a reason. Not
  having noticed it is not a reason.
